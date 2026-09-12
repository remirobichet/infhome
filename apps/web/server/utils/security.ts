import { createHash, timingSafeEqual } from 'node:crypto'
import { createError, getHeader, setHeader, useSession, type H3Event } from 'h3'

function authConfig(event: H3Event) {
  const config = useRuntimeConfig(event)
  if (!config.adminUsername || !config.adminPassword || config.sessionPassword.length < 32) {
    throw createError({ statusCode: 503, message: 'Configuration de connexion incomplète sur le serveur.' })
  }
  return config
}

export function equalSecret(left: string, right: string) {
  const hash = (value: string) => createHash('sha256').update(value).digest()
  return timingSafeEqual(hash(left), hash(right))
}

export function adminSession(event: H3Event) {
  const config = authConfig(event)
  return useSession<{ username?: string, credentialTag?: string }>(event, {
    name: 'infhome-session', password: config.sessionPassword, maxAge: 60 * 60 * 24 * 7,
    cookie: { httpOnly: true, secure: !import.meta.dev, sameSite: 'strict', path: '/' },
    sessionHeader: false,
  })
}

export function credentialTag(event: H3Event) {
  const config = authConfig(event)
  return createHash('sha256').update(JSON.stringify([config.adminUsername, config.adminPassword])).digest('hex')
}

export async function currentUser(event: H3Event) {
  const session = await adminSession(event)
  const config = authConfig(event)
  return session.data.username === config.adminUsername && equalSecret(session.data.credentialTag ?? '', credentialTag(event))
    ? config.adminUsername : null
}

export async function requireAdmin(event: H3Event) {
  if (!await currentUser(event)) throw createError({ statusCode: 401, message: 'Connectez-vous pour continuer.' })
}

export function requireSameOrigin(event: H3Event) {
  const expected = new URL(useRuntimeConfig(event).siteUrl).origin
  if (getHeader(event, 'origin') !== expected) {
    throw createError({ statusCode: 403, message: 'Origine de la requête refusée.' })
  }
}

// Une seule instance et un seul administrateur : limite globale, sans confiance dans X-Forwarded-For.
const requests = new Map<string, number[]>()
export function rateLimit(event: H3Event, key: 'login' | 'publish', limit: number, windowMs: number) {
  const now = Date.now()
  const recent = (requests.get(key) ?? []).filter(time => time > now - windowMs)
  requests.set(key, recent)
  if (recent.length >= limit) {
    setHeader(event, 'Retry-After', Math.ceil((recent[0]! + windowMs - now) / 1000))
    throw createError({ statusCode: 429, message: 'Trop de tentatives. Réessayez dans quelques minutes.' })
  }
  recent.push(now)
}

export async function readLimitedJson(event: H3Event, limit: number): Promise<unknown> {
  if (getHeader(event, 'content-type')?.split(';')[0]?.trim() !== 'application/json') {
    throw createError({ statusCode: 415, message: 'Un document JSON est attendu.' })
  }
  if (Number(getHeader(event, 'content-length')) > limit) {
    throw createError({ statusCode: 413, message: 'Document trop volumineux.' })
  }
  const chunks: Buffer[] = []
  let size = 0
  for await (const chunk of event.node.req) {
    const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk)
    size += buffer.length
    if (size > limit) throw createError({ statusCode: 413, message: 'Document trop volumineux.' })
    chunks.push(buffer)
  }
  try { return JSON.parse(Buffer.concat(chunks).toString('utf8')) }
  catch { throw createError({ statusCode: 400, message: 'Document JSON invalide.' }) }
}
