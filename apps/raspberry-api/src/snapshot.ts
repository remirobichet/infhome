export type Snapshot = {
  version: 1;
  updatedAt: number;
  shopping: string[];
  agenda: { title: string; startDate: string; endDate: string | null; time: string | null }[];
};

export function record(value: unknown): Record<string, unknown> {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("Expected object");
  return value as Record<string, unknown>;
}

function keys(value: Record<string, unknown>, expected: string[]) {
  if (Object.keys(value).length !== expected.length || expected.some(key => !(key in value))) {
    throw new Error("Unexpected fields");
  }
}

function text(value: unknown, max: number) {
  if (typeof value !== "string" || !value.length || value.trim() !== value ||
      Buffer.byteLength(value) > max || /[\u0000-\u001f\u007f-\u009f]/u.test(value)) throw new Error("Invalid text");
}

export function date(value: unknown): value is string {
  if (typeof value !== "string" || !/^20\d{2}-\d{2}-\d{2}$/.test(value)) return false;
  const parsed = new Date(`${value}T00:00:00Z`);
  return Number.isFinite(parsed.getTime()) && parsed.toISOString().slice(0, 10) === value;
}

export function validateSnapshot(input: unknown): Snapshot {
  const value = record(input);
  keys(value, ["version", "updatedAt", "shopping", "agenda"]);
  if (value.version !== 1 || !Number.isSafeInteger(value.updatedAt) || (value.updatedAt as number) < 0) throw new Error("Invalid version or timestamp");
  if (!Array.isArray(value.shopping) || value.shopping.length > 20) throw new Error("Invalid shopping list");
  value.shopping.forEach(item => text(item, 64));
  if (!Array.isArray(value.agenda) || value.agenda.length > 3) throw new Error("Invalid agenda");
  for (const item of value.agenda) {
    const event = record(item);
    keys(event, ["title", "startDate", "endDate", "time"]);
    text(event.title, 96);
    if (!date(event.startDate) || !(event.endDate === null || (date(event.endDate) && event.endDate >= event.startDate))) throw new Error("Invalid event dates");
    if (!(event.time === null || (typeof event.time === "string" && /^([01]\d|2[0-3]):[0-5]\d$/.test(event.time)))) throw new Error("Invalid event time");
  }
  if (Buffer.byteLength(JSON.stringify(value)) > 4096) throw new Error("Snapshot too large");
  return value as Snapshot;
}
