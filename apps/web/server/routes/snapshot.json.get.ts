import { createError, defineEventHandler, setHeaders } from 'h3'
import { LIMITS, publicSnapshot } from '../../shared/snapshot'

export default defineEventHandler(async (event) => {
  const stored = await snapshotStore().read()
  if (!stored.revision) throw createError({ statusCode: 503, message: 'Aucun contenu publié pour le moment.' })
  const json = JSON.stringify(publicSnapshot(stored))
  const size = Buffer.byteLength(json)
  if (size > LIMITS.snapshotBytes) throw createError({ statusCode: 500, message: 'Le snapshot dépasse la taille autorisée.' })
  setHeaders(event, {
    'Content-Type': 'application/json; charset=utf-8', 'Content-Length': size,
    'Cache-Control': 'no-store', 'Connection': 'close',
  })
  return json
})
