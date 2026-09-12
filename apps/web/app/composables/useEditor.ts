import { contentSchema, shoppingLines, type AgendaEvent, type StoredSnapshot } from '#shared/snapshot'
import { errorMessage } from '~/lib/errors'

export function useEditor() {
  const shopping = ref('')
  const agenda = ref<AgendaEvent[]>([])
  const saved = ref<StoredSnapshot | null>(null)
  const saving = ref(false)
  const message = ref('')
  const failure = ref('')
  const submitted = ref(false)
  const conflict = ref(false)
  const tab = ref('shopping')
  const content = computed(() => ({ shopping: shoppingLines(shopping.value), agenda: agenda.value }))
  const validation = computed(() => contentSchema.safeParse(content.value))
  const dirty = computed(() => !!saved.value && JSON.stringify(content.value) !== JSON.stringify({ shopping: saved.value.shopping, agenda: saved.value.agenda }))
  const issues = computed(() => submitted.value && !validation.value.success ? validation.value.error.issues : [])
  const shoppingErrors = computed(() => issues.value.filter(issue => issue.path[0] === 'shopping').map(issue => `${typeof issue.path[1] === 'number' ? `Ligne ${issue.path[1] + 1} : ` : ''}${issue.message}`))

  function eventErrors(index: number) {
    return Object.fromEntries(issues.value.filter(issue => issue.path[0] === 'agenda' && issue.path[1] === index)
      .map(issue => [String(issue.path[2]), issue.message]))
  }

  function load(snapshot: StoredSnapshot) {
    saved.value = structuredClone(toRaw(snapshot))
    shopping.value = snapshot.shopping.join('\n')
    agenda.value = snapshot.agenda.map(event => ({ ...event }))
    submitted.value = false
    conflict.value = false
    failure.value = ''
    message.value = ''
  }

  async function publish() {
    submitted.value = true
    failure.value = ''
    message.value = ''
    if (!validation.value.success) {
      tab.value = shoppingErrors.value.length ? 'shopping' : 'agenda'
      failure.value = 'Corrigez les champs indiqués avant de publier.'
      return
    }
    if (!saved.value || saving.value) return
    saving.value = true
    try {
      const snapshot = await $fetch('/api/admin/snapshot', {
        method: 'POST', body: { ...validation.value.data, revision: saved.value.revision },
      })
      load(snapshot)
      message.value = 'Publication réussie. Vos modifications sont en ligne.'
    }
    catch (cause) {
      const status = (cause as { statusCode?: number }).statusCode
      conflict.value = status === 409
      failure.value = status === 401
        ? 'Session expirée. Reconnectez-vous dans un autre onglet, puis republiez : vos modifications restent ici.'
        : errorMessage(cause, 'Publication impossible. Vos modifications sont conservées ici. Réessayez.')
    }
    finally { saving.value = false }
  }

  watch(content, () => { message.value = '' }, { deep: true, flush: 'sync' })

  return { shopping, agenda, saved, saving, message, failure, submitted, conflict, tab, dirty, shoppingErrors, eventErrors, load, publish }
}
