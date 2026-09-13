import { date, record } from "./snapshot";

export type Period = { max: number; code: number; label: string };
export type Weather = { date: string; morning: Period; evening: Period };

const labels: Record<number, string> = {
  0: "Ensoleillé", 1: "Peu nuageux", 2: "Nuageux", 3: "Couvert", 45: "Brouillard", 48: "Brouillard givrant",
  51: "Bruine", 53: "Bruine", 55: "Bruine", 56: "Bruine verglaçante", 57: "Bruine verglaçante",
  61: "Pluvieux", 63: "Pluvieux", 65: "Pluvieux", 66: "Pluie verglaçante", 67: "Pluie verglaçante",
  71: "Neige", 73: "Neige", 75: "Neige", 77: "Neige", 80: "Averses", 81: "Averses", 82: "Averses",
  85: "Averses de neige", 86: "Averses de neige", 95: "Orageux", 96: "Orage et grêle", 99: "Orage et grêle",
};

function severity(code: number) {
  if (code >= 95) return 5;
  if ([56, 57, 66, 67].includes(code)) return 4;
  if ([71, 73, 75, 77, 85, 86].includes(code)) return 3;
  if (code >= 51) return 2;
  return 0;
}

function summarize(rows: { temperature: number; code: number }[]): Period {
  const priority = Math.max(...rows.map(row => severity(row.code)));
  const counts = new Map<number, number>();
  rows.filter(row => severity(row.code) === priority).forEach(row => counts.set(row.code, (counts.get(row.code) ?? 0) + 1));
  const code = [...counts].sort((a, b) => b[1] - a[1] || b[0] - a[0])[0][0];
  return { max: Math.max(...rows.map(row => row.temperature)), code, label: labels[code] };
}

export function localDate(now = new Date(), timezone = "Europe/Paris") {
  const parts = new Intl.DateTimeFormat("en-GB", { timeZone: timezone, year: "numeric", month: "2-digit", day: "2-digit" }).formatToParts(now);
  const part = (name: string) => parts.find(value => value.type === name)!.value;
  return `${part("year")}-${part("month")}-${part("day")}`;
}

export function parseWeather(input: unknown, today: string): Weather {
  const root = record(input);
  const units = record(root.hourly_units);
  if (units.temperature_2m !== "°C") throw new Error("Unexpected temperature unit");
  const hourly = record(root.hourly);
  const times = hourly.time, temperatures = hourly.temperature_2m, codes = hourly.weather_code;
  if (!Array.isArray(times) || !Array.isArray(temperatures) || !Array.isArray(codes) ||
      times.length !== temperatures.length || times.length !== codes.length) throw new Error("Invalid hourly weather");
  const morning: { temperature: number; code: number }[] = [], evening: typeof morning = [];
  const hours = new Set<number>();
  for (let i = 0; i < times.length; i++) {
    if (typeof times[i] !== "string" || !times[i].startsWith(`${today}T`)) continue;
    if (!/^20\d{2}-\d{2}-\d{2}T([01]\d|2[0-3]):00$/.test(times[i])) throw new Error("Invalid weather hour");
    const hour = Number(times[i].slice(11, 13));
    const temperature = temperatures[i], code = codes[i];
    if (typeof temperature !== "number" || !Number.isFinite(temperature) || temperature < -100 || temperature > 70 ||
        !Number.isInteger(code) || !(code in labels)) throw new Error("Invalid weather values");
    hours.add(hour);
    (hour < 12 ? morning : evening).push({ temperature, code });
  }
  // Au passage à l'heure d'été, 02:00 peut être absent ; au retour, il peut être répété.
  const expected = Array.from({ length: 24 }, (_, hour) => hour);
  const springTransition = new Date(`${today}T12:00:00Z`).getUTCDay() === 0 && today.slice(5, 7) === "03" && Number(today.slice(8)) >= 25;
  if (expected.some(hour => !hours.has(hour) && !(springTransition && hour === 2))) throw new Error("Incomplete weather day");
  return { date: today, morning: summarize(morning), evening: summarize(evening) };
}

export function validateWeather(input: unknown): Weather {
  const value = record(input);
  if (!date(value.date)) throw new Error("Invalid weather date");
  for (const name of ["morning", "evening"]) {
    const period = record(value[name]);
    if (typeof period.max !== "number" || !Number.isFinite(period.max) || period.max < -100 || period.max > 70 ||
        !Number.isInteger(period.code) || labels[period.code as number] === undefined ||
        labels[period.code as number] !== period.label) throw new Error("Invalid cached weather");
  }
  return { date: value.date, morning: value.morning as Period, evening: value.evening as Period };
}
