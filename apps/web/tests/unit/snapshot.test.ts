import { randomUUID } from 'node:crypto'
import { describe, expect, it } from 'vitest'
import { LIMITS, agendaEventSchema, contentSchema, isUpcoming, publicSnapshot, type AgendaEvent, type StoredSnapshot } from '../../shared/snapshot'

function event(startDate: string, fields: Partial<AgendaEvent> = {}): AgendaEvent {
  return { id: randomUUID(), title: 'Rendez-vous', startDate, endDate: null, time: null, ...fields }
}

function stored(agenda: AgendaEvent[]): StoredSnapshot {
  return { version: 1, updatedAt: 1789200000, revision: randomUUID(), shopping: [], agenda }
}

describe('contrat v1', () => {
  it('refuse les dates inexistantes, plages inversées et heures invalides', () => {
    for (const invalid of [event('2026-02-29'), event('2026-13-01'), event('2026-09-12', { endDate: '2026-09-11' }), event('2026-09-12', { time: '24:00' })]) {
      expect(agendaEventSchema.safeParse(invalid).success).toBe(false)
    }
    expect(agendaEventSchema.safeParse(event('2028-02-29')).success).toBe(true)
  })

  it('limite les octets UTF-8 et refuse les champs inconnus et les identifiants dupliqués', () => {
    expect(contentSchema.safeParse({ shopping: ['é'.repeat(33)], agenda: [] }).success).toBe(false)
    expect(contentSchema.safeParse({ shopping: ['Pain\nLait'], agenda: [] }).success).toBe(false)
    expect(contentSchema.safeParse({ shopping: [], agenda: [], admin: true }).success).toBe(false)
    const same = event('2026-09-12')
    expect(contentSchema.safeParse({ shopping: [], agenda: [same, same] }).success).toBe(false)
  })

  it('sélectionne trois prochains événements, même très éloignés, sans exposer les identifiants privés', () => {
    const result = publicSnapshot(stored([
      event('2029-01-01'), event('2026-09-11'), event('2027-01-01'), event('2026-09-13'), event('2030-01-01'),
    ]), new Date('2026-09-12T10:00:00Z'))
    expect(result.agenda.map(item => item.startDate)).toEqual(['2026-09-13', '2027-01-01', '2029-01-01'])
    expect(result).not.toHaveProperty('revision')
    expect(result.agenda[0]).not.toHaveProperty('id')
  })

  it('garde les périodes en cours et journées entières, écarte les rendez-vous passés', () => {
    const now = new Date('2026-09-12T10:00:00Z') // 12:00 à Paris.
    expect(isUpcoming(event('2026-09-12', { time: '11:59' }), now)).toBe(false)
    expect(isUpcoming(event('2026-09-12', { time: '12:00' }), now)).toBe(true)
    expect(isUpcoming(event('2026-09-12'), now)).toBe(true)
    expect(isUpcoming(event('2026-09-10', { endDate: '2026-09-12', time: '09:00' }), now)).toBe(true)
    expect(isUpcoming(event('2026-09-10', { endDate: '2026-09-12' }), new Date('2026-09-12T22:00:00Z'))).toBe(false)
  })

  it('applique Europe/Paris en hiver, en été et lors du changement de jour', () => {
    expect(isUpcoming(event('2026-01-12', { time: '12:00' }), new Date('2026-01-12T11:01:00Z'))).toBe(false)
    expect(isUpcoming(event('2026-07-12', { time: '12:00' }), new Date('2026-07-12T10:01:00Z'))).toBe(false)
    expect(isUpcoming(event('2026-03-29'), new Date('2026-03-29T22:00:00Z'))).toBe(false)
  })

  it('change la sélection à la lecture sans nouvelle publication', () => {
    const source = stored([event('2026-09-12'), event('2026-09-13'), event('2026-09-14'), event('2026-09-15')])
    const before = publicSnapshot(source, new Date('2026-09-12T10:00:00Z'))
    const after = publicSnapshot(source, new Date('2026-09-13T10:00:00Z'))
    expect(before.agenda[0]!.startDate).toBe('2026-09-12')
    expect(after.agenda[0]!.startDate).toBe('2026-09-13')
    expect(before.updatedAt).toBe(after.updatedAt)
    expect(source.agenda).toHaveLength(4)
  })

  it('reste sous 4 Kio même avec les longueurs maximales et les caractères JSON échappés', () => {
    const source = stored(Array.from({ length: LIMITS.agenda }, () => event('2099-12-31', {
      endDate: '2099-12-31', title: '"'.repeat(LIMITS.titleBytes), time: '23:59',
    })))
    source.shopping = Array.from({ length: LIMITS.shopping }, () => '\\'.repeat(LIMITS.itemBytes))
    expect(contentSchema.safeParse({ shopping: source.shopping, agenda: source.agenda }).success).toBe(true)
    expect(Buffer.byteLength(JSON.stringify(publicSnapshot(source)))).toBeLessThanOrEqual(LIMITS.snapshotBytes)
    expect(Buffer.byteLength(JSON.stringify(source))).toBeLessThanOrEqual(LIMITS.bodyBytes)
  })
})
