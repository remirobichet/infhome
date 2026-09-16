const { test } = require('node:test');
const assert = require('node:assert/strict');
const { mkdtemp, readFile, writeFile, rm } = require('node:fs/promises');
const { tmpdir } = require('node:os');
const { join } = require('node:path');
const { createServer } = require('node:http');
const { connect } = require('node:net');
const { once } = require('node:events');
const { Cache, downloadJson } = require('../dist/cache');
const { validateSnapshot } = require('../dist/snapshot');
const { parseWeather, validateWeather, localDate } = require('../dist/weather');
const { createApi } = require('../dist/server');
const { configuration } = require('../dist/config');
const { contentRefresher } = require('../dist/content');

const snapshot = () => ({ version: 1, updatedAt: 1789200000, shopping: ['Pain', 'Thé'], agenda: [
  { title: 'Dentiste', startDate: '2026-09-23', endDate: null, time: '18:30' },
] });

function forecast(day = '2026-09-13') {
  return { hourly_units: { temperature_2m: '°C' }, hourly: {
    time: Array.from({ length: 24 }, (_, hour) => `${day}T${String(hour).padStart(2, '0')}:00`),
    temperature_2m: Array.from({ length: 24 }, (_, hour) => hour < 12 ? 15 + hour : 30 - (hour - 12)),
    weather_code: Array(24).fill(3),
  } };
}

test('snapshot strict, dates civiles et limites UTF-8', () => {
  assert.deepEqual(validateSnapshot(snapshot()), snapshot());
  for (const mutate of [
    v => v.version = 2, v => v.updatedAt = true, v => v.revision = 'private',
    v => v.shopping = ['é'.repeat(33)], v => v.shopping = ['\nPain'],
    v => v.shopping = Array(21).fill('Pain'), v => v.agenda[0].startDate = '2026-02-30',
    v => v.agenda[0].endDate = '2026-09-22', v => delete v.agenda[0].time,
    v => v.agenda[0].time = '24:00', v => v.agenda[0].id = 'private',
  ]) {
    const value = snapshot(); mutate(value); assert.throws(() => validateSnapshot(value));
  }
});

test('météo par demi-journée : maxima, dominante et priorité précipitations', () => {
  const value = forecast();
  value.hourly.weather_code[3] = 61;
  value.hourly.weather_code[18] = 95;
  const weather = parseWeather(value, '2026-09-13');
  assert.deepEqual(weather, { date: '2026-09-13', morning: { max: 26, code: 61, label: 'Pluvieux' },
    evening: { max: 30, code: 95, label: 'Orageux' } });
  assert.deepEqual(validateWeather(weather), weather);
  assert.throws(() => validateWeather({ ...weather, morning: { max: 20, code: 999 } }));
  assert.equal(parseWeather(forecast(), '2026-09-13').morning.label, 'Couvert');
  value.hourly.temperature_2m[1] = null;
  assert.throws(() => parseWeather(value, '2026-09-13'));
  assert.throws(() => parseWeather(forecast(), '2026-09-14'));
});

test('météo : journée incomplète refusée, changement heure Paris accepté', () => {
  const removeHour = (value, hour) => Object.values(value.hourly).forEach(list => list.splice(hour, 1));
  const incomplete = forecast(); removeHour(incomplete, 2);
  assert.throws(() => parseWeather(incomplete, '2026-09-13'));
  const spring = forecast('2026-03-29'); removeHour(spring, 2);
  assert.equal(parseWeather(spring, '2026-03-29').date, '2026-03-29');
  assert.equal(localDate(new Date('2026-09-13T22:01:00Z')), '2026-09-14');
});

test('cache : conservation après erreur, remplacement même updatedAt, redémarrage hors ligne', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'infhome-test-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const path = join(directory, 'content.json');
  const cache = new Cache(path, validateSnapshot);
  await cache.load(); assert.equal(cache.value, null);
  await cache.replace(snapshot(), 100);
  const before = await readFile(path, 'utf8');
  await assert.rejects(cache.replace({ ...snapshot(), version: 2 }, 200));
  assert.equal(await readFile(path, 'utf8'), before);
  const next = snapshot(); next.agenda = [];
  await cache.replace(next, 300);
  const restarted = new Cache(path, validateSnapshot); await restarted.load();
  assert.deepEqual(restarted.value, { data: next, fetchedAt: 300 });
  await writeFile(path, '{broken');
  const logged = t.mock.method(console, 'error', () => {});
  const corrupt = new Cache(path, validateSnapshot); await corrupt.load();
  assert.equal(corrupt.value, null);
  assert.equal(logged.mock.callCount(), 1);
  assert.equal(await readFile(path, 'utf8'), '{broken');
});

async function listen(server, t) {
  server.listen(0, '127.0.0.1'); await once(server, 'listening');
  t.after(() => new Promise(resolve => { server.close(resolve); server.closeAllConnections(); }));
  return `http://127.0.0.1:${server.address().port}`;
}

test('téléchargement borné : HTTP, JSON, UTF-8, taille, timeout et retour du réseau', async t => {
  let mode = 'http';
  const remote = createServer((req, res) => {
    if (mode === 'http') { res.writeHead(503); res.end(); }
    if (mode === 'json') res.end('{broken');
    if (mode === 'utf8') res.end(Buffer.from([0xff]));
    if (mode === 'size') res.end(' '.repeat(4097));
    if (mode === 'slow') { res.writeHead(200); res.write('{'); }
    if (mode === 'ok') res.end(JSON.stringify(snapshot()));
  });
  const url = await listen(remote, t);
  for (mode of ['http', 'json', 'utf8', 'size', 'slow']) await assert.rejects(downloadJson(url, 4096, 100));
  mode = 'ok'; assert.deepEqual(await downloadJson(url, 4096), snapshot());
});

test('HTTP PSP : hors ligne, stale, framing HTTP/1.0, méthodes et taille', async t => {
  const content = { value: null }, weather = { value: null };
  const server = createApi(content, weather, 900000, () => null, async () => { throw new Error('Unexpected refresh'); });
  const url = await listen(server, t);
  let response = await fetch(`${url}/api/v1/dashboard`);
  let body = await response.json();
  assert.equal(body.content, null); assert.equal(body.weather, null); assert.equal(body.time.synced, null);
  content.value = { data: snapshot(), fetchedAt: Math.floor(Date.now() / 1000) };
  weather.value = { data: parseWeather(forecast(), '2026-09-13'), fetchedAt: 1 };
  response = await fetch(`${url}/api/v1/dashboard`); body = await response.json();
  assert.deepEqual(body.content, snapshot()); assert.equal(body.contentSync.stale, false); assert.equal(body.weather.stale, true);
  content.value.fetchedAt = 1;
  assert.equal((await (await fetch(`${url}/api/v1/dashboard`)).json()).contentSync.stale, true);
  const socket = connect(server.address().port, '127.0.0.1');
  const chunks = []; socket.on('data', chunk => chunks.push(chunk));
  socket.end('GET /api/v1/dashboard HTTP/1.0\r\n\r\n'); await once(socket, 'close');
  const raw = Buffer.concat(chunks); const split = raw.indexOf('\r\n\r\n');
  const headers = raw.subarray(0, split).toString(); const bytes = raw.subarray(split + 4);
  assert.match(headers, /connection: close/i);
  assert.doesNotMatch(headers, /transfer-encoding|content-encoding/i);
  assert.equal(Number(headers.match(/content-length: (\d+)/i)[1]), bytes.length);
  assert.ok(bytes.length <= 8192);
  assert.equal((await fetch(`${url}/api/status`, { method: 'POST' })).status, 405);
  assert.equal((await fetch(`${url}/missing`)).status, 404);
  assert.deepEqual(await (await fetch(`${url}/api/status`)).json(), { message: 'Hello from Raspberry Pi' });
});

test('configuration validée et valeurs Toulouse', () => {
  const config = configuration({});
  assert.equal(config.intervalMs, 900000); assert.equal(config.latitude, 43.6045);
  assert.throws(() => configuration({ PORT: '1.5' }));
  assert.throws(() => configuration({ INFHOME_SNAPSHOT_URL: 'http://example.com' }));
  assert.throws(() => configuration({ INFHOME_LATITUDE: 'NaN' }));
});

test('POST actualise courses/agenda, partage la synchronisation et conserve le cache après échec', async t => {
  const directory = await mkdtemp(join(tmpdir(), 'infhome-refresh-'));
  t.after(() => rm(directory, { recursive: true, force: true }));
  const content = new Cache(join(directory, 'content.json'), validateSnapshot);
  await content.replace(snapshot(), 1);
  const weather = { value: { data: parseWeather(forecast(), '2026-09-13'), fetchedAt: 123 } };
  const previousWeather = structuredClone(weather.value);
  let calls = 0, release;
  let arrived;
  let arrival = new Promise(resolve => { arrived = resolve; });
  const remote = createServer((req, res) => {
    calls++;
    release = body => res.end(body);
    arrived();
  });
  const remoteUrl = await listen(remote, t);
  const refresh = contentRefresher(content, remoteUrl, 1000);
  const url = await listen(createApi(content, weather, 900000, () => true, refresh), t);
  const next = snapshot(); next.shopping = ['Pommes']; next.agenda = [];
  // A scheduled refresh and repeated calls share the same in-flight download.
  const scheduled = refresh();
  assert.equal(refresh(), scheduled);
  await arrival;
  let joined;
  const joinedRefresh = new Promise(resolve => { joined = resolve; });
  const manualUrl = await listen(createApi(content, weather, 900000, () => true, () => {
    const pending = refresh(); joined(); return pending;
  }), t);
  const manual = fetch(`${manualUrl}/api/v1/dashboard/refresh`, { method: 'POST' });
  await joinedRefresh;
  assert.equal(calls, 1);
  const cached = await (await fetch(`${url}/api/v1/dashboard`)).json();
  assert.deepEqual(cached.content, snapshot());
  release(JSON.stringify(next));
  const response = await manual;
  assert.equal(response.status, 200);
  assert.deepEqual((await response.json()).content, next);
  await scheduled;
  assert.deepEqual(weather.value, previousWeather);
  assert.ok(content.value.fetchedAt > 1);
  const saved = await readFile(content.path, 'utf8');
  t.mock.method(console, 'error', () => {});
  arrival = new Promise(resolve => { arrived = resolve; });
  const failed = fetch(`${url}/api/v1/dashboard/refresh`, { method: 'POST' });
  await arrival;
  release('{broken');
  assert.equal((await failed).status, 502);
  assert.equal(await readFile(content.path, 'utf8'), saved);
  assert.deepEqual(content.value.data, next);
  arrival = new Promise(resolve => { arrived = resolve; });
  const socket = connect(Number(new URL(url).port), '127.0.0.1');
  const chunks = []; socket.on('data', chunk => chunks.push(chunk));
  const closed = once(socket, 'close');
  socket.write('POST /api/v1/dashboard/refresh HTTP/1.0\r\nContent-Length: 0\r\nConnection: close\r\n\r\n');
  await arrival;
  release(JSON.stringify(snapshot()));
  await closed;
  const raw = Buffer.concat(chunks), split = raw.indexOf('\r\n\r\n');
  const headers = raw.subarray(0, split).toString(), body = raw.subarray(split + 4);
  assert.match(headers, /^HTTP\/1\.1 200/);
  assert.match(headers, /connection: close/i);
  assert.doesNotMatch(headers, /transfer-encoding|content-encoding/i);
  assert.equal(Number(headers.match(/content-length: (\d+)/i)[1]), body.length);
  assert.deepEqual(JSON.parse(body).content, snapshot());
  assert.deepEqual(content.value.data, snapshot());
  assert.deepEqual(weather.value, previousWeather);
  for (const method of ['GET', 'HEAD', 'PUT']) {
    const denied = await fetch(`${url}/api/v1/dashboard/refresh`, { method });
    assert.equal(denied.status, 405);
    assert.equal(denied.headers.get('allow'), 'POST');
  }
  assert.equal(calls, 3);
});
