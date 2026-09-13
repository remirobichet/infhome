import { join } from "node:path";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import { Cache, downloadJson } from "./cache";
import { configuration } from "./config";
import { validateSnapshot } from "./snapshot";
import { localDate, parseWeather, validateWeather } from "./weather";
import { createApi } from "./server";

async function main() {
  const config = configuration();
  const content = new Cache(join(config.dataDir, "content.json"), validateSnapshot);
  const weather = new Cache(join(config.dataDir, "weather.json"), validateWeather);
  await Promise.all([content.load(), weather.load()]);
  let synced: boolean | null = null;
  const server = createApi(content, weather, config.intervalMs, () => synced);
  server.requestTimeout = 10000;
  server.headersTimeout = 10000;
  server.setTimeout(10000, socket => socket.destroy());
  server.maxConnections = 32;
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(config.port, config.host, resolve);
  });
  console.log(`Infhome listening on ${config.host}:${config.port}`);

  const timers = new Set<NodeJS.Timeout>();
  let stopping = false;
  function loop(name: string, interval: number, action: () => Promise<void>) {
    const run = async () => {
      try { await action(); } catch (error) { console.error(`${name} failed:`, error); }
      if (!stopping) {
        const timer = setTimeout(() => { timers.delete(timer); void run(); }, interval);
        timers.add(timer);
      }
    };
    void run();
  }
  loop("content", config.intervalMs, async () => {
    await content.replace(await downloadJson(config.snapshotUrl, 4096, config.timeoutMs), Math.floor(Date.now() / 1000));
    console.log("Content synchronized");
  });
  loop("weather", config.intervalMs, async () => {
    const today = localDate();
    const url = new URL("https://api.open-meteo.com/v1/forecast");
    url.search = new URLSearchParams({ latitude: String(config.latitude), longitude: String(config.longitude),
      hourly: "temperature_2m,weather_code", timezone: config.timezone, temperature_unit: "celsius",
      start_date: today, end_date: today }).toString();
    const data = parseWeather(await downloadJson(url.href, 65536, config.timeoutMs), today);
    await weather.replace(data, Math.floor(Date.now() / 1000));
    console.log("Weather synchronized");
  });
  loop("ntp", 60000, async () => {
    try {
      const { stdout } = await promisify(execFile)("timedatectl", ["show", "--property=NTPSynchronized", "--value"], { timeout: 2000 });
      synced = stdout.trim() === "yes" ? true : stdout.trim() === "no" ? false : null;
    } catch { synced = null; }
  });
  function stop() {
    stopping = true;
    timers.forEach(clearTimeout);
    server.close();
    server.closeIdleConnections();
    setTimeout(() => process.exit(0), config.timeoutMs + 1000).unref();
  }
  process.once("SIGTERM", stop);
  process.once("SIGINT", stop);
}

main().catch(error => { console.error(error); process.exitCode = 1; });
