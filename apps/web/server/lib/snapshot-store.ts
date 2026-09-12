import { randomUUID } from 'node:crypto'
import { mkdir, open, rename, unlink } from 'node:fs/promises'
import { join } from 'node:path'
import { LIMITS, publishSchema, storedSnapshotSchema, type PublishInput, type StoredSnapshot } from '../../shared/snapshot'

export class RevisionConflict extends Error {}

export function createSnapshotStore(directory: string) {
  const filename = join(directory, 'snapshot.json')
  let pending: Promise<unknown> = Promise.resolve()

  async function read(): Promise<StoredSnapshot> {
    try {
      const file = await open(filename, 'r')
      try {
        if ((await file.stat()).size > LIMITS.bodyBytes) throw new Error('Le snapshot enregistré dépasse la taille autorisée.')
        return storedSnapshotSchema.parse(JSON.parse(await file.readFile('utf8')))
      }
      finally { await file.close() }
    }
    catch (error) {
      if ((error as NodeJS.ErrnoException).code === 'ENOENT') {
        return { version: 1, updatedAt: 0, revision: null, shopping: [], agenda: [] }
      }
      throw error
    }
  }

  function publish(input: PublishInput): Promise<StoredSnapshot> {
    const parsed = publishSchema.parse(input)
    const operation = pending.then(async () => {
      const current = await read()
      if (current.revision !== parsed.revision) throw new RevisionConflict('Une autre publication existe.')
      const next: StoredSnapshot = {
        version: 1, updatedAt: Math.floor(Date.now() / 1000), revision: randomUUID(),
        shopping: parsed.shopping, agenda: parsed.agenda,
      }
      const json = JSON.stringify(next)
      if (Buffer.byteLength(json) > LIMITS.bodyBytes) throw new Error('Le snapshot est trop volumineux.')
      await mkdir(directory, { recursive: true, mode: 0o700 })
      const temporary = join(directory, `.snapshot-${randomUUID()}.tmp`)
      try {
        const file = await open(temporary, 'wx', 0o600)
        try {
          await file.writeFile(json, 'utf8')
          await file.sync()
        }
        finally { await file.close() }
        await rename(temporary, filename)
      }
      finally { await unlink(temporary).catch(() => {}) }
      return next
    })
    pending = operation.catch(() => {})
    return operation
  }

  return { read, publish }
}
