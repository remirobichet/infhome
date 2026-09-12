import { createError, defineEventHandler } from 'h3'
import { z } from 'zod'

const loginSchema = z.object({ username: z.string().min(1).max(128), password: z.string().min(1).max(512) }).strict()

export default defineEventHandler(async (event) => {
  requireSameOrigin(event)
  rateLimit(event, 'login', 10, 15 * 60 * 1000)
  const input = loginSchema.safeParse(await readLimitedJson(event, 2048))
  if (!input.success) throw createError({ statusCode: 400, message: 'Identifiant et mot de passe requis.' })
  const session = await adminSession(event)
  const config = useRuntimeConfig(event)
  const usernameMatches = equalSecret(input.data.username, config.adminUsername)
  const passwordMatches = equalSecret(input.data.password, config.adminPassword)
  if (!usernameMatches || !passwordMatches) {
    throw createError({ statusCode: 401, message: 'Identifiant ou mot de passe incorrect.' })
  }
  await session.clear()
  await session.update({ username: config.adminUsername, credentialTag: credentialTag(event) })
  return { username: config.adminUsername }
})
