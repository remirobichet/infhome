import { defineEventHandler } from 'h3'

export default defineEventHandler(async (event) => {
  await requireAdmin(event)
  return snapshotStore().read()
})
