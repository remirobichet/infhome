export default defineNuxtRouteMiddleware(async (to) => {
  const auth = useAuth()
  try { await auth.refresh() }
  catch {
    // La page de connexion présente les erreurs de configuration lors de la tentative.
    auth.username.value = null
  }
  if (!auth.username.value && to.path !== '/login') return navigateTo('/login')
  if (auth.username.value && to.path === '/login') return navigateTo('/')
})
