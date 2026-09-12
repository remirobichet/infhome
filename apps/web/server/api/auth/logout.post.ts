import { defineEventHandler } from 'h3'

export default defineEventHandler(async (event) => {
  requireSameOrigin(event)
  await requireAdmin(event)
  await (await adminSession(event)).clear()
  return { ok: true }
})
