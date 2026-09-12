import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { createSnapshotStore, RevisionConflict } from '../../server/lib/snapshot-store'

let directory: string
beforeEach(async () => { directory = await mkdtemp(join(tmpdir(), 'infhome-store-')) })
afterEach(async () => { await rm(directory, { recursive: true, force: true }) })

describe('stockage persistant', () => {
  it('publie et restaure le contenu après recréation du service', async () => {
    const store = createSnapshotStore(directory)
    expect((await store.read()).revision).toBeNull()
    const published = await store.publish({ revision: null, shopping: ['Pain', 'Café'], agenda: [] })
    expect(await createSnapshotStore(directory).read()).toEqual(published)
    expect(await readdir(directory)).toEqual(['snapshot.json'])
  })

  it('sérialise les publications concurrentes et refuse les révisions périmées', async () => {
    const store = createSnapshotStore(directory)
    const first = store.publish({ revision: null, shopping: ['Pain'], agenda: [] })
    const second = store.publish({ revision: null, shopping: ['Lait'], agenda: [] })
    await first
    await expect(second).rejects.toBeInstanceOf(RevisionConflict)
    const current = await store.read()
    expect(current.shopping).toEqual(['Pain'])
    await store.publish({ revision: current.revision, shopping: [], agenda: [] })
    expect((await store.read()).shopping).toEqual([])
  })

  it('une entrée invalide ne remplace jamais le fichier valide', async () => {
    const store = createSnapshotStore(directory)
    const current = await store.publish({ revision: null, shopping: ['Pain'], agenda: [] })
    const before = await readFile(join(directory, 'snapshot.json'), 'utf8')
    expect(() => store.publish({ revision: current.revision, shopping: ['x'.repeat(65)], agenda: [] })).toThrow()
    expect(await readFile(join(directory, 'snapshot.json'), 'utf8')).toBe(before)
  })

  it('refuse un fichier corrompu au lieu de le remplacer par un contenu vide', async () => {
    await writeFile(join(directory, 'snapshot.json'), '{broken')
    const store = createSnapshotStore(directory)
    await expect(store.read()).rejects.toThrow()
    await expect(store.publish({ revision: null, shopping: [], agenda: [] })).rejects.toThrow()
    expect(await readFile(join(directory, 'snapshot.json'), 'utf8')).toBe('{broken')
  })
})
