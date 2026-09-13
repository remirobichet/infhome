import { mkdir, open, rename, unlink } from "node:fs/promises";
import { dirname } from "node:path";
import { randomUUID } from "node:crypto";
import { record } from "./snapshot";

export type Cached<T> = { fetchedAt: number; data: T };

export class Cache<T> {
  value: Cached<T> | null = null;
  constructor(readonly path: string, private validate: (input: unknown) => T) {}

  async load() {
    try {
      const handle = await open(this.path, "r");
      let raw: string;
      try {
        if ((await handle.stat()).size > 16384) throw new Error("Cache too large");
        raw = await handle.readFile("utf8");
      } finally { await handle.close(); }
      const value = record(JSON.parse(raw));
      if (!Number.isSafeInteger(value.fetchedAt) || (value.fetchedAt as number) < 0) throw new Error("Invalid cache timestamp");
      this.value = { fetchedAt: value.fetchedAt as number, data: this.validate(value.data) };
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== "ENOENT") console.error(`Cache unavailable: ${this.path}`, error);
    }
  }

  async replace(data: unknown, fetchedAt: number) {
    const next = { data: this.validate(data), fetchedAt };
    await mkdir(dirname(this.path), { recursive: true });
    const temp = `${this.path}.${randomUUID()}.tmp`;
    try {
      const handle = await open(temp, "wx", 0o600);
      try { await handle.writeFile(JSON.stringify(next)); await handle.sync(); }
      finally { await handle.close(); }
      await rename(temp, this.path);
      this.value = next;
    } finally { await unlink(temp).catch(() => {}); }
  }
}

export async function downloadJson(url: string, maxBytes: number, timeoutMs = 8000): Promise<unknown> {
  const response = await fetch(url, { signal: AbortSignal.timeout(timeoutMs), redirect: "error", headers: { Accept: "application/json" } });
  if (!response.ok) { await response.body?.cancel(); throw new Error(`HTTP ${response.status}`); }
  if (!response.body) throw new Error("Empty response");
  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let size = 0;
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.length;
      if (size > maxBytes) throw new Error("Response too large");
      chunks.push(value);
    }
    return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(Buffer.concat(chunks)));
  } finally { await reader.cancel().catch(() => {}); }
}
