import { createError, defineEventHandler } from 'h3'
import { LIMITS, publishSchema } from '../../../shared/snapshot'
import { RevisionConflict } from '../../lib/snapshot-store'

export default defineEventHandler(async (event) => {
  requireSameOrigin(event)
  await requireAdmin(event)
  rateLimit(event, 'publish', 30, 60 * 1000)
  const result = publishSchema.safeParse(await readLimitedJson(event, LIMITS.bodyBytes))
  if (!result.success) {
    throw createError({ statusCode: 422, message: 'Vérifiez les données du formulaire.', data: { issues: result.error.issues } })
  }
  try { return await snapshotStore().publish(result.data) }
  catch (error) {
    if (error instanceof RevisionConflict) {
      throw createError({ statusCode: 409, message: 'Une autre session a publié entre-temps. Rechargez la version publiée avant de réessayer.' })
    }
    throw error
  }
})
