import { Cache, downloadJson } from "./cache";
import type { Snapshot } from "./snapshot";

export function contentRefresher(content: Cache<Snapshot>, url: string, timeoutMs: number) {
  let pending: Promise<void> | null = null;
  return () => {
    if (!pending) {
      pending = (async () => {
        await content.replace(await downloadJson(url, 4096, Math.min(timeoutMs, 45000)), Math.floor(Date.now() / 1000));
        console.log("Content synchronized");
      })().finally(() => { pending = null; });
    }
    return pending;
  };
}
