import tailwindcss from '@tailwindcss/vite'

export default defineNuxtConfig({
  compatibilityDate: '2026-09-12',
  devtools: { enabled: false },
  modules: ['shadcn-nuxt', '@nuxtjs/color-mode'],
  css: ['~/assets/css/tailwind.css'],
  vite: { plugins: [tailwindcss()] },
  shadcn: { prefix: 'Ui', componentDir: './app/components/ui' },
  colorMode: { classSuffix: '', preference: 'system', fallback: 'light' },
  runtimeConfig: {
    adminUsername: '',
    adminPassword: '',
    sessionPassword: '',
    dataDir: './data',
    siteUrl: 'http://localhost:3000',
  },
  app: {
    head: {
      htmlAttrs: { lang: 'fr' },
      title: 'Infhome — Édition',
      meta: [{ name: 'robots', content: 'noindex, nofollow' }],
      link: [{ rel: 'icon', type: 'image/svg+xml', href: '/favicon.svg' }],
    },
  },
  nitro: { preset: 'node-server' },
  typescript: { strict: true },
})
