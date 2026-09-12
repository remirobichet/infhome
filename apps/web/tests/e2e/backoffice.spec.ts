import { randomUUID } from 'node:crypto'
import { expect, test } from '@playwright/test'

const origin = 'http://localhost:3100'
const credentials = { username: 'test-admin', password: 'test-only-password' }

test('connexion, formulaires mobiles, publication, échec réseau et réinitialisation', async ({ page, request }, testInfo) => {
  const errors: string[] = []
  page.on('pageerror', error => errors.push(error.message))
  page.on('console', message => { if (message.type() === 'warning' && /hydration/i.test(message.text())) errors.push(message.text()) })
  expect((await request.get('/api/admin/snapshot')).status()).toBe(401)
  expect((await request.get('/snapshot.json')).status()).toBe(503)
  await page.goto('/')
  await expect(page).toHaveURL('/login')
  await expect(page.getByRole('heading', { name: 'Connexion' })).toBeVisible()
  await page.screenshot({ path: testInfo.outputPath('connexion-mobile.png'), fullPage: true })
  await page.getByLabel('Identifiant', { exact: true }).fill(credentials.username)
  await page.getByLabel('Mot de passe', { exact: true }).fill(credentials.password)
  await page.getByRole('button', { name: 'Se connecter', exact: true }).click()
  await expect(page).toHaveURL('/')
  await page.getByLabel('Articles à prévoir').fill('Pain\nCafé\nPommes')
  await expect(page.getByText('Modifications en attente')).toBeVisible()
  await page.getByRole('tab', { name: /Agenda/ }).click()
  await page.getByRole('button', { name: 'Ajouter un événement' }).click()
  await page.getByLabel('Titre', { exact: true }).fill('Week-end en famille')
  await page.getByLabel('Date de début', { exact: true }).fill('2090-09-12')
  await page.getByLabel('Fin (facultative)', { exact: true }).fill('2090-09-14')
  await page.getByLabel('Heure (facultative)', { exact: true }).fill('18:30')
  await page.screenshot({ path: testInfo.outputPath('agenda-mobile.png'), fullPage: true })
  await page.getByRole('button', { name: 'Publier', exact: true }).click()
  await expect(page.getByRole('status')).toContainText('Publication réussie')
  let published = await (await request.get('/snapshot.json')).json()
  expect(published.shopping).toEqual(['Pain', 'Café', 'Pommes'])
  expect(published.agenda).toEqual([{ title: 'Week-end en famille', startDate: '2090-09-12', endDate: '2090-09-14', time: '18:30' }])

  await page.reload()
  await page.getByRole('tab', { name: /Agenda/ }).click()
  await expect(page.getByLabel('Titre', { exact: true })).toHaveValue('Week-end en famille')
  await page.getByRole('button', { name: 'Activer le thème sombre' }).click()
  await expect(page.locator('html')).toHaveClass(/dark/)
  await page.screenshot({ path: testInfo.outputPath('agenda-sombre-mobile.png'), fullPage: true })
  await page.setViewportSize({ width: 340, height: 780 })
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true)
  await page.setViewportSize({ width: 1280, height: 900 })
  await page.screenshot({ path: testInfo.outputPath('agenda-desktop.png'), fullPage: true })
  await page.setViewportSize({ width: 390, height: 844 })

  await page.getByRole('tab', { name: 'Courses' }).click()
  await page.getByLabel('Articles à prévoir').fill('Lait')
  await page.route('**/api/admin/snapshot', route => route.request().method() === 'POST'
    ? route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ message: 'Serveur temporairement indisponible.' }) }) : route.continue())
  await page.getByRole('button', { name: 'Publier', exact: true }).click()
  await expect(page.getByRole('alert')).toContainText('Serveur temporairement indisponible')
  await expect(page.getByLabel('Articles à prévoir')).toHaveValue('Lait')
  published = await (await request.get('/snapshot.json')).json()
  expect(published.shopping).toEqual(['Pain', 'Café', 'Pommes'])
  await page.unroute('**/api/admin/snapshot')
  await page.getByRole('button', { name: 'Publier', exact: true }).click()
  await expect(page.getByRole('status')).toContainText('Publication réussie')

  await page.getByRole('button', { name: 'Réinitialiser la liste', exact: true }).click()
  await page.getByRole('button', { name: 'Réinitialiser', exact: true }).click()
  await expect(page.getByLabel('Articles à prévoir')).toHaveValue('')
  expect((await (await request.get('/snapshot.json')).json()).shopping).toEqual(['Lait'])
  await page.getByRole('button', { name: 'Publier', exact: true }).click()
  await expect(page.getByRole('status')).toContainText('Publication réussie')
  expect((await (await request.get('/snapshot.json')).json()).shopping).toEqual([])
  await page.getByRole('button', { name: 'Se déconnecter', exact: true }).click()
  await page.getByRole('alertdialog').getByRole('button', { name: 'Se déconnecter', exact: true }).click()
  await expect(page).toHaveURL('/login')
  expect(errors).toEqual([])
})

test('API : cookie sécurisé, validation, conflit et contrat public borné', async ({ request }) => {
  const login = await request.post('/api/auth/login', { headers: { origin }, data: credentials })
  expect(login.status()).toBe(200)
  const cookie = login.headers()['set-cookie']!
  expect(cookie).toContain('HttpOnly')
  expect(cookie).toContain('Secure')
  expect(cookie).toContain('SameSite=Strict')
  // Le client API ne traite pas localhost comme Chromium pour les cookies Secure.
  const sessionCookie = cookie.split('\n').at(-1)!.split(';')[0]!
  const headers = { origin, cookie: sessionCookie }
  const source = await (await request.get('/api/admin/snapshot', { headers })).json()
  const input = {
    revision: source.revision,
    shopping: ['Pain'],
    agenda: Array.from({ length: 5 }, (_, i) => ({ id: randomUUID(), title: `Rendez-vous ${i}`, startDate: `2090-10-${String(i + 10).padStart(2, '0')}`, endDate: null, time: null })),
  }
  expect((await request.post('/api/admin/snapshot', { headers: { ...headers, origin: 'https://example.org' }, data: input })).status()).toBe(403)
  expect((await request.post('/api/admin/snapshot', { headers, data: { ...input, shopping: ['é'.repeat(33)] } })).status()).toBe(422)
  expect((await request.post('/api/admin/snapshot', { headers, data: { ...input, extra: true } })).status()).toBe(422)
  expect((await request.post('/api/admin/snapshot', { headers, data: { ...input, shopping: ['x'.repeat(70000)] } })).status()).toBe(413)
  expect((await request.post('/api/admin/snapshot', { headers, data: input })).status()).toBe(200)
  expect((await request.post('/api/admin/snapshot', { headers, data: input })).status()).toBe(409)
  const response = await request.get('/snapshot.json')
  const json = await response.json()
  expect(json.agenda).toHaveLength(3)
  expect(json.agenda[0].title).toBe('Rendez-vous 0')
  expect(json).not.toHaveProperty('revision')
  expect(json.agenda[0]).not.toHaveProperty('id')
  expect(response.headers()['cache-control']).toBe('no-store')
  expect(response.headers()['content-encoding']).toBeUndefined()
  expect(response.headers()['transfer-encoding']).toBeUndefined()
  expect(Number(response.headers()['content-length'])).toBe((await response.body()).length)
  expect((await response.body()).length).toBeLessThanOrEqual(4096)
  expect((await request.post('/api/auth/logout', { headers })).status()).toBe(200)
})

test('la connexion refuse les origines étrangères et limite les tentatives', async ({ request }) => {
  expect((await request.post('/api/auth/login', { headers: { origin: 'https://example.org' }, data: credentials })).status()).toBe(403)
  let throttled = false
  for (let i = 0; i < 11; i++) {
    const result = await request.post('/api/auth/login', { headers: { origin }, data: { ...credentials, password: 'incorrect' } })
    if (result.status() === 429) {
      expect(Number(result.headers()['retry-after'])).toBeGreaterThan(0)
      throttled = true
      break
    }
    expect(result.status()).toBe(401)
  }
  expect(throttled).toBe(true)
})
