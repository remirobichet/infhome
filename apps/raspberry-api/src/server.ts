import { createServer } from "node:http";
import type { Cache } from "./cache";
import type { Snapshot } from "./snapshot";
import { localDate, type Weather } from "./weather";

export function createApi(content: Cache<Snapshot>, weather: Cache<Weather>, intervalMs: number, synced: () => boolean | null) {
  return createServer((req, res) => {
    let status = 200;
    let payload: unknown;
    if (req.method !== "GET" && req.method !== "HEAD") {
      status = 405; payload = { error: "Method not allowed" }; res.setHeader("Allow", "GET, HEAD");
    } else if (req.url === "/api/status") {
      payload = { message: "Hello from Raspberry Pi" };
    } else if (req.url === "/api/v1/dashboard") {
      const now = Math.floor(Date.now() / 1000);
      const stale = (fetchedAt: number) => now < fetchedAt || now - fetchedAt > intervalMs / 1000 * 2;
      const currentWeather = weather.value;
      payload = {
        version: 1, generatedAt: now,
        time: { unix: now, timezone: "Europe/Paris", synced: synced() },
        weather: currentWeather ? { ...currentWeather.data, fetchedAt: currentWeather.fetchedAt,
          stale: stale(currentWeather.fetchedAt) || currentWeather.data.date !== localDate() } : null,
        content: content.value?.data ?? null,
        contentSync: { fetchedAt: content.value?.fetchedAt ?? null, stale: !content.value || stale(content.value.fetchedAt) },
      };
    } else { status = 404; payload = { error: "Not found" }; }
    let body = Buffer.from(JSON.stringify(payload));
    if (body.length > 8192) { status = 500; body = Buffer.from('{"error":"Dashboard too large"}'); }
    res.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Content-Length": body.length,
      "Cache-Control": "no-store", Connection: "close" });
    res.end(req.method === "HEAD" ? undefined : body);
  });
}
