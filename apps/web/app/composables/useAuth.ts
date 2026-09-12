export function useAuth() {
  const username = useState<string | null>('auth-username', () => null)
  const requestFetch = useRequestFetch()

  async function refresh() {
    const session = await requestFetch('/api/auth/session')
    username.value = session.username
  }

  async function login(user: string, password: string) {
    const session = await $fetch('/api/auth/login', { method: 'POST', body: { username: user, password } })
    username.value = session.username
  }

  async function logout() {
    await $fetch('/api/auth/logout', { method: 'POST' })
    username.value = null
    await navigateTo('/login')
  }

  return { username, refresh, login, logout }
}
