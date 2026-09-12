import { defineConfig, devices } from '@playwright/test'

export default defineConfig({
  testDir: './tests/e2e',
  workers: 1,
  fullyParallel: false,
  reporter: 'list',
  use: {
    baseURL: 'http://localhost:3100',
    ...devices['iPhone 13'],
    defaultBrowserType: 'chromium',
    trace: 'retain-on-failure',
  },
  webServer: {
    command: 'node tests/start-server.mjs',
    url: 'http://localhost:3100/health',
    reuseExistingServer: false,
    timeout: 30000,
  },
})
