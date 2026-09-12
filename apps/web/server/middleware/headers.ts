import { defineEventHandler, setHeaders } from 'h3'

export default defineEventHandler((event) => {
  setHeaders(event, {
    'X-Content-Type-Options': 'nosniff',
    'X-Frame-Options': 'DENY',
    'Referrer-Policy': 'same-origin',
    'X-Robots-Tag': 'noindex, nofollow',
  })
  if (!event.path.startsWith('/_nuxt/')) setHeaders(event, { 'Cache-Control': 'no-store' })
})
