import { resolve } from "node:path";

export function configuration(env = process.env) {
  const number = (name: string, fallback: number, min: number, max: number) => {
    const value = Number(env[name] ?? fallback);
    if (!Number.isFinite(value) || value < min || value > max) throw new Error(`Invalid ${name}`);
    return value;
  };
  const snapshotUrl = env.INFHOME_SNAPSHOT_URL ?? "https://infhome.remirobichet.fr/snapshot.json";
  if (new URL(snapshotUrl).protocol !== "https:") throw new Error("Snapshot URL must use HTTPS");
  const port = number("PORT", 8080, 1, 65535);
  if (!Number.isInteger(port)) throw new Error("Invalid PORT");
  return {
    host: env.HOST ?? "0.0.0.0", port, snapshotUrl,
    latitude: number("INFHOME_LATITUDE", 43.6045, -90, 90),
    longitude: number("INFHOME_LONGITUDE", 1.444, -180, 180),
    timezone: "Europe/Paris",
    intervalMs: number("INFHOME_REFRESH_SECONDS", 900, 10, 86400) * 1000,
    timeoutMs: number("INFHOME_TIMEOUT_SECONDS", 8, 1, 60) * 1000,
    dataDir: resolve(env.INFHOME_DATA_DIR ?? "./data"),
  };
}
