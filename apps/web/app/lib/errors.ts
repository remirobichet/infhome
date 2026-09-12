export function errorMessage(error: unknown, fallback: string) {
  const data = (error as { data?: { message?: string, statusMessage?: string }, statusCode?: number })?.data
  return data?.message || data?.statusMessage || fallback
}
