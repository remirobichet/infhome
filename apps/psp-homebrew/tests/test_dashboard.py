"""Black-box tests of the actual portable C parser, transport checks and clock.

Run from any directory with Python 3 and a native C compiler:
    python3 apps/psp-homebrew/tests/test_dashboard.py
Optional: CC=gcc CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
Optional live contract check: INFHOME_TEST_URL=http://192.168.0.104:8080/api/v1/dashboard
"""

import copy
import datetime as dt
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile
import unittest
import urllib.request
from zoneinfo import ZoneInfo


ROOT = Path(__file__).resolve().parents[1]
BASE = {
    "version": 1,
    "generatedAt": 1789300800,
    "time": {"unix": 1789300800, "timezone": "Europe/Paris", "synced": True},
    "weather": {
        "date": "2026-09-13",
        "morning": {"max": -2.5, "code": 3, "label": "Couvert"},
        "evening": {"max": 28.1, "code": 61, "label": "Pluvieux"},
        "fetchedAt": 1789300700,
        "stale": False,
    },
    "content": {
        "version": 1,
        "updatedAt": 1789200000,
        "shopping": ["Café", 'Pain "complet"', "Crème brûlée", "Cœur", "Lait\\nature"],
        "agenda": [
            {"title": "Week-end en famille", "startDate": "2026-09-19", "endDate": "2026-09-20", "time": "10:00"},
            {"title": "Dentiste", "startDate": "2026-09-23", "endDate": None, "time": None},
        ],
    },
    "contentSync": {"fetchedAt": 1789300700, "stale": False},
}


def encoded(value, **kwargs):
    return json.dumps(value, separators=(",", ":"), **kwargs).encode()


class DashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="infhome-test-")
        cls.executable = str(Path(cls.directory.name) / "probe")
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c99", "-Wall", "-Wextra", "-Werror", "-g",
            *shlex.split(os.environ.get("CFLAGS", "")), "-I", str(ROOT),
            str(ROOT / "dashboard.c"), str(ROOT / "tests/probe.c"), "-o", cls.executable,
        ], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def probe(self, data, mode="json", valid=True):
        process = subprocess.run([self.executable, mode], input=data, capture_output=True)
        self.assertEqual(process.returncode, 0 if valid else 1, process.stderr.decode(errors="replace"))
        return process.stdout.decode()

    def test_realistic_snapshot_and_unicode(self):
        for ascii_only in (True, False):
            output = self.probe(encoded(BASE, ensure_ascii=ascii_only))
            self.assertIn("1 1 5 2 1 -2.5 28.1", output)
            self.assertIn('Cafe\nPain "complet"\nCreme brulee\nCoeur\nLait\\nature', output)
            self.assertIn("2026-09-19|2026-09-20|10:00", output)
            self.assertIn("2026-09-23||", output)

    def test_missing_sources_and_empty_lists(self):
        for weather in (None, BASE["weather"]):
            for content in (None, {"version": 1, "updatedAt": 1789200000, "shopping": [], "agenda": []}):
                data = copy.deepcopy(BASE)
                data["weather"] = weather
                data["content"] = content
                if content is None:
                    data["contentSync"] = {"fetchedAt": None, "stale": True}
                for synced in (None, False, True):
                    data["time"]["synced"] = synced
                    self.probe(encoded(data))

    def test_maximum_lists_and_utf8_byte_limits(self):
        data = copy.deepcopy(BASE)
        data["content"]["shopping"] = ["é" * 32] * 20
        data["content"]["agenda"] = [dict(BASE["content"]["agenda"][0], title="é" * 48)] * 3
        self.probe(encoded(data, ensure_ascii=False))
        data["content"]["shopping"].append("Pain")
        self.probe(encoded(data, ensure_ascii=False), valid=False)
        data["content"]["shopping"].pop()
        data["content"]["shopping"][0] += "é"
        self.probe(encoded(data, ensure_ascii=False), valid=False)
        data["content"]["shopping"][0] = "Pain"
        data["content"]["agenda"][0]["title"] += "é"
        self.probe(encoded(data, ensure_ascii=False), valid=False)

    def test_rejected_field_types_versions_dates(self):
        cases = [
            (("version",), 2), (("version",), True), (("version",), 1.0),
            (("time", "unix"), -1), (("time", "unix"), 4102444800),
            (("time", "timezone"), "UTC"), (("time", "synced"), "true"),
            (("weather", "morning", "max"), 1e100),
            (("weather", "morning", "code"), -1),
            (("weather", "date"), "2026-02-29"),
            (("content", "version"), 2), (("content", "shopping"), [None]),
            (("content", "shopping"), [""]), (("content", "shopping"), ["a\nb"]),
            (("content", "agenda"), [BASE["content"]["agenda"][0]] * 4),
            (("content", "agenda", 0, "endDate"), "2026-09-18"),
            (("content", "agenda", 0, "time"), "24:00"),
            (("content", "agenda", 0, "time"), "09:60"),
            (("content", "agenda", 0, "time"), "9:30"),
            (("contentSync", "fetchedAt"), None),
        ]
        for path, replacement in cases:
            with self.subTest(path=path, value=replacement):
                data = copy.deepcopy(BASE)
                parent = data
                for key in path[:-1]:
                    parent = parent[key]
                parent[path[-1]] = replacement
                self.probe(encoded(data), valid=False)

    def test_strict_json_and_atomic_replacement(self):
        body = encoded(BASE)
        for broken in [
            body[:-1], body + b"{}", body + b"\0", body.replace(b'"version":1', b'"version":01', 1),
            body.replace(b'"version":1', b'"version":1,"version":1', 1),
            body.replace(b'"version":1', b'"version":1,"extra":0', 1),
            body.replace(b'"version":1,', b"", 1),
            body.replace(b'"Caf\\u00e9"', b'"\\ud800"'),
            body.replace(b'"Caf\\u00e9"', b'"\xc0\xaf"'),
            body.replace(b'"Caf\\u00e9"', b'"\\u0000"'),
            body.replace(b'"Caf\\u00e9"', b'"\\x41"'),
            b"[" * 14 + b"0" + b"]" * 14,
        ]:
            self.probe(broken, valid=False)
        self.probe(body.replace(b'"version"', b'"ver\\u0073ion"'))
        self.probe(body + b" " * (8192 - len(body)))
        self.probe(body + b" " * (8193 - len(body)), valid=False)

    def test_unicode_surrogates_and_combining_marks(self):
        data = copy.deepcopy(BASE)
        data["content"]["shopping"] = ["Café ☀ 😀", "Cafe\u0301", "l’œuf — frais…"]
        output = self.probe(encoded(data))
        self.assertIn("Cafe ? ?\nCafe\nl'oeuf - frais...", output)

    def test_http_headers(self):
        base = b"HTTP/1.0 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nContent-Length: 8192\r\nConnection: close\r\n\r\n"
        self.assertEqual(self.probe(base, "http"), "8192\n")
        self.probe(base.replace(b"HTTP/1.0", b"HTTP/1.1").replace(b"Content-Length", b"content-length"), "http")
        for broken in [
            base.replace(b"200", b"503"), base.replace(b"200 OK", b"2000 OK"),
            base.replace(b"8192", b"8193"), base.replace(b"8192", b"0"), base.replace(b"8192", b"-1"),
            base.replace(b"8192", b"9999999999999999999999999"),
            base.replace(b"Content-Length: 8192\r\n", b""),
            base.replace(b"Content-Length: 8192\r\n", b"Content-Length: 12\r\nContent-Length: 12\r\n"),
            base.replace(b"Connection: close", b"Transfer-Encoding: chunked"),
            base.replace(b"Connection: close", b"Content-Encoding: gzip"),
            base.replace(b"application/json", b"text/html"), base[:-1], base + b"x",
            base.replace(b"Connection: close", b" Long: folded"),
            base.replace(b"Connection: close", b"Long: " + b"a" * 2048),
        ]:
            self.probe(broken, "http", valid=False)

    def test_paris_clock_dst_leap_midnight_and_2038(self):
        paris = ZoneInfo("Europe/Paris")
        moments = [
            "2026-03-29T00:59:59", "2026-03-29T01:00:00",
            "2026-10-25T00:59:59", "2026-10-25T01:00:00",
            "2026-09-13T21:59:59", "2026-09-13T22:00:00",
            "2028-02-28T23:00:00", "2038-01-19T03:14:08", "2099-12-31T23:59:59",
        ]
        for iso in moments:
            utc = dt.datetime.fromisoformat(iso).replace(tzinfo=dt.timezone.utc)
            expected = utc.astimezone(paris)
            output = self.probe(str(int(utc.timestamp())).encode(), "time").strip()
            self.assertEqual(output, expected.strftime("%Y-%m-%d %H:%M:%S") + f" {(expected.weekday() + 1) % 7}")

    def test_deterministic_malformed_input_stress(self):
        randomizer = random.Random(20260913)
        body = encoded(BASE)
        for _ in range(200):
            broken = bytearray(body)
            broken[randomizer.randrange(len(broken))] = 0
            self.probe(bytes(broken), valid=False)

    @unittest.skipUnless(os.environ.get("INFHOME_TEST_URL"), "INFHOME_TEST_URL non défini")
    def test_live_dashboard(self):
        with urllib.request.urlopen(os.environ["INFHOME_TEST_URL"], timeout=10) as response:
            self.assertEqual(response.status, 200)
            data = response.read(8193)
        self.probe(data)


if __name__ == "__main__":
    unittest.main(verbosity=2)
