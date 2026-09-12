import { spawn } from 'node:child_process'
import { randomBytes } from 'node:crypto'
import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const directory = await mkdtemp(join(tmpdir(), 'infhome-e2e-'))
const server = spawn(process.execPath, ['.output/server/index.mjs'], {
  stdio: 'inherit',
  env: {
    ...process.env,
    NODE_ENV: 'production',
    PORT: '3100',
    HOST: '0.0.0.0',
    NUXT_ADMIN_USERNAME: 'test-admin',
    NUXT_ADMIN_PASSWORD: 'test-only-password',
    NUXT_SESSION_PASSWORD: randomBytes(48).toString('hex'),
    NUXT_SITE_URL: 'http://localhost:3100',
    NUXT_DATA_DIR: directory,
  },
})
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => server.kill(signal))
server.on('exit', async (code) => {
  await rm(directory, { recursive: true, force: true })
  process.exit(code ?? 0)
})
