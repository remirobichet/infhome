import { z } from 'zod'

export const LIMITS = { shopping: 20, itemBytes: 64, agenda: 100, titleBytes: 96, snapshotBytes: 4096, bodyBytes: 65536 } as const
export const TIMEZONE = 'Europe/Paris'

export function byteLength(value: string) {
  return new TextEncoder().encode(value).length
}

function shortText(max: number) {
  return z.string().trim().min(1, 'Ce champ est obligatoire.').refine(
    value => !/[\u0000-\u001f\u007f-\u009f]/u.test(value),
    'Les caractères de contrôle ne sont pas acceptés.',
  ).refine(value => byteLength(value) <= max, `Texte trop long : ${max} octets UTF-8 maximum (les accents comptent davantage).`)
}

const dateSchema = z.string().regex(/^(20\d{2})-\d{2}-\d{2}$/, 'Date attendue entre 2000 et 2099.').refine(value => {
  const date = new Date(`${value}T00:00:00Z`)
  return !Number.isNaN(date.getTime()) && date.toISOString().slice(0, 10) === value
}, 'Cette date n’existe pas.')

export const agendaEventSchema = z.object({
  id: z.uuid(),
  title: shortText(LIMITS.titleBytes),
  startDate: dateSchema,
  endDate: dateSchema.nullable(),
  time: z.string().regex(/^([01]\d|2[0-3]):[0-5]\d$/, 'Heure invalide.').nullable(),
}).strict().refine(event => !event.endDate || event.endDate >= event.startDate, {
  message: 'La fin doit être postérieure ou égale au début.', path: ['endDate'],
})

export const contentSchema = z.object({
  shopping: z.array(shortText(LIMITS.itemBytes)).max(LIMITS.shopping, `Maximum ${LIMITS.shopping} articles.`),
  agenda: z.array(agendaEventSchema).max(LIMITS.agenda, `Maximum ${LIMITS.agenda} événements.`).refine(
    events => new Set(events.map(event => event.id)).size === events.length,
    'Les identifiants des événements doivent être uniques.',
  ),
}).strict()

export const publishSchema = contentSchema.extend({ revision: z.uuid().nullable() })
export const storedSnapshotSchema = contentSchema.extend({
  version: z.literal(1),
  updatedAt: z.number().int().nonnegative(),
  revision: z.uuid().nullable(),
})

export type AgendaEvent = z.infer<typeof agendaEventSchema>
export type Content = z.infer<typeof contentSchema>
export type StoredSnapshot = z.infer<typeof storedSnapshotSchema>
export type PublishInput = z.infer<typeof publishSchema>

export function parisNow(now: Date) {
  const parts = new Intl.DateTimeFormat('en-GB', {
    timeZone: TIMEZONE, year: 'numeric', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
  }).formatToParts(now)
  const part = (type: Intl.DateTimeFormatPartTypes) => parts.find(value => value.type === type)!.value
  return { date: `${part('year')}-${part('month')}-${part('day')}`, time: `${part('hour')}:${part('minute')}` }
}

export function isUpcoming(event: AgendaEvent, now: Date) {
  const current = parisNow(now)
  const end = event.endDate ?? event.startDate
  if (end !== current.date) return end > current.date
  // Une période reste active jusqu'à la fin du dernier jour. L'heure concerne son début.
  if (end > event.startDate || !event.time) return true
  return event.time >= current.time
}

export function compareEvents(a: AgendaEvent, b: AgendaEvent) {
  return a.startDate.localeCompare(b.startDate) || (a.time ?? '').localeCompare(b.time ?? '') || a.id.localeCompare(b.id)
}

export function publicSnapshot(stored: StoredSnapshot, now = new Date()) {
  return {
    version: stored.version,
    updatedAt: stored.updatedAt,
    shopping: stored.shopping,
    agenda: stored.agenda.filter(event => isUpcoming(event, now)).sort(compareEvents).slice(0, 3)
      .map(({ title, startDate, endDate, time }) => ({ title, startDate, endDate, time })),
  }
}

export function shoppingLines(text: string) {
  return text.split(/\r?\n/).map(line => line.trim()).filter(Boolean)
}
