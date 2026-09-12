<script setup lang="ts">
import { LIMITS, parisNow } from '#shared/snapshot'
import { errorMessage } from '~/lib/errors'

const auth = useAuth()
const { data, error, status, refresh } = await useFetch('/api/admin/snapshot')
const { shopping, agenda, saved, saving, message, failure, conflict, tab, dirty, shoppingErrors, eventErrors, load, publish } = useEditor()
watch(data, value => { if (value) load(value) }, { immediate: true })
const leaving = ref(false)
const publicationDate = computed(() => saved.value?.updatedAt
  ? new Intl.DateTimeFormat('fr-FR', { timeZone: 'Europe/Paris', day: 'numeric', month: 'long', year: 'numeric' }).format(new Date(saved.value.updatedAt * 1000)) : null)

function addEvent() {
  if (agenda.value.length < LIMITS.agenda) {
    agenda.value.push({ id: crypto.randomUUID(), title: '', startDate: parisNow(new Date()).date, endDate: null, time: null })
    nextTick(() => document.getElementById(`title-${agenda.value.at(-1)!.id}`)?.focus())
  }
}

async function logout() {
  try {
    leaving.value = true
    await auth.logout()
  }
  catch (cause) {
    leaving.value = false
    failure.value = errorMessage(cause, 'Déconnexion impossible. Réessayez.')
  }
}

function beforeUnload(event: BeforeUnloadEvent) {
  if (dirty.value && !leaving.value) { event.preventDefault(); event.returnValue = '' }
}
onMounted(() => window.addEventListener('beforeunload', beforeUnload))
onBeforeUnmount(() => window.removeEventListener('beforeunload', beforeUnload))
</script>

<template>
  <div class="mx-auto max-w-3xl px-5 pb-40 sm:px-8">
    <header class="flex items-center justify-between border-b py-5">
      <BrandMark />
      <div class="flex items-center gap-1">
        <ThemeToggle />
        <UiAlertDialog>
          <UiAlertDialogTrigger as-child><UiButton variant="ghost" size="icon" aria-label="Se déconnecter" :disabled="saving || leaving"><AppIcon name="logout" /></UiButton></UiAlertDialogTrigger>
          <UiAlertDialogContent>
            <UiAlertDialogHeader>
              <UiAlertDialogTitle>Se déconnecter ?</UiAlertDialogTitle>
              <UiAlertDialogDescription>{{ dirty ? 'Les modifications non publiées seront perdues.' : 'Votre contenu publié restera disponible.' }}</UiAlertDialogDescription>
            </UiAlertDialogHeader>
            <UiAlertDialogFooter><UiAlertDialogCancel>Rester ici</UiAlertDialogCancel><UiAlertDialogAction @click="logout">Se déconnecter</UiAlertDialogAction></UiAlertDialogFooter>
          </UiAlertDialogContent>
        </UiAlertDialog>
      </div>
    </header>

    <main id="main">
      <div class="pb-8 pt-10 sm:pt-14">
        <p class="eyebrow mb-4">VOTRE ESPACE / ÉDITION</p>
        <h1 class="text-3xl font-medium tracking-tight sm:text-4xl">À la maison<span class="text-primary">.</span></h1>
        <p class="mt-3 max-w-sm text-sm leading-relaxed text-muted-foreground">Préparez votre liste et vos rendez-vous.<br>Publiez quand tout est prêt.</p>
      </div>

      <div v-if="status === 'pending'" aria-label="Chargement des données" aria-busy="true" class="space-y-5">
        <div class="h-12 rounded-lg bg-muted" /><div class="h-72 rounded-xl bg-muted" />
      </div>
      <div v-else-if="error" role="alert" class="space-y-4 rounded-xl border p-6">
        <h2 class="font-medium">Le contenu n’a pas pu être chargé.</h2>
        <p class="text-sm text-muted-foreground">Vérifiez votre connexion et réessayez. Si le problème persiste, vérifiez le stockage du serveur.</p>
        <UiButton variant="outline" @click="refresh()">Réessayer</UiButton>
      </div>
      <form v-else-if="saved" id="editor" novalidate :aria-busy="saving" @submit.prevent="publish">
        <fieldset :disabled="saving" class="min-w-0">
          <legend class="sr-only">Contenu à publier</legend>
          <UiTabs v-model="tab" class="gap-7">
            <UiTabsList class="grid h-12 w-full grid-cols-2 rounded-xl bg-muted p-1">
              <UiTabsTrigger value="shopping" class="h-full gap-2 rounded-lg"><span class="size-2.5 border border-current" aria-hidden="true" />Courses</UiTabsTrigger>
              <UiTabsTrigger value="agenda" class="h-full gap-2 rounded-lg"><span class="size-2.5 rounded-full border border-current" aria-hidden="true" />Agenda<span class="ml-1 font-mono text-xs text-muted-foreground">{{ agenda.length }}</span></UiTabsTrigger>
            </UiTabsList>
            <UiTabsContent value="shopping"><ShoppingEditor v-model="shopping" :errors="shoppingErrors" /></UiTabsContent>
            <UiTabsContent value="agenda">
              <section aria-labelledby="agenda-title">
                <div class="mb-6">
                  <h2 id="agenda-title" class="text-xl font-medium tracking-tight">Les prochains rendez-vous</h2>
                  <p class="mt-1 text-sm leading-relaxed text-muted-foreground">Les 3 prochains événements sont sélectionnés automatiquement. Une période en cours reste visible jusqu’à son dernier jour.</p>
                </div>
                <div v-if="!agenda.length" class="mb-5 rounded-xl border border-dashed px-6 py-10 text-center">
                  <span class="mx-auto mb-4 block size-6 rounded-full border border-muted-foreground/40" aria-hidden="true" />
                  <h3 class="font-medium">De la place pour vos projets.</h3>
                  <p class="mt-2 text-sm text-muted-foreground">Ajoutez un rendez-vous ou une période à retenir.</p>
                </div>
                <div v-else class="mb-5 space-y-4">
                  <AgendaEventEditor v-for="(event, index) in agenda" :key="event.id" v-model="agenda[index]!" :index="index" :errors="eventErrors(index)" @remove="agenda.splice(index, 1)" />
                </div>
                <UiButton type="button" variant="outline" class="w-full border-dashed" :disabled="agenda.length >= LIMITS.agenda" @click="addEvent"><AppIcon name="plus" />Ajouter un événement</UiButton>
                <p class="mt-3 text-right font-mono text-xs text-muted-foreground">{{ agenda.length }} / {{ LIMITS.agenda }}</p>
              </section>
            </UiTabsContent>
          </UiTabs>
        </fieldset>
      </form>
    </main>

    <div v-if="saved && !error" class="fixed inset-x-0 bottom-0 z-20 border-t bg-background/95 backdrop-blur-md">
      <div class="mx-auto max-w-3xl px-5 pb-[max(1rem,env(safe-area-inset-bottom))] pt-4 sm:px-8">
        <p v-if="message" role="status" class="mb-3 flex items-center gap-2 text-sm text-primary"><AppIcon name="check" />{{ message }}</p>
        <div v-if="failure" role="alert" class="mb-3 text-sm text-destructive">
          <p>{{ failure }}</p>
          <a v-if="failure.startsWith('Session expirée')" href="/login" target="_blank" rel="noopener" class="mt-2 inline-block underline">Ouvrir la connexion dans un autre onglet</a>
          <UiAlertDialog v-if="conflict">
            <UiAlertDialogTrigger as-child><UiButton variant="outline" class="mt-3">Recharger la version publiée</UiButton></UiAlertDialogTrigger>
            <UiAlertDialogContent>
              <UiAlertDialogHeader><UiAlertDialogTitle>Recharger le contenu publié ?</UiAlertDialogTitle><UiAlertDialogDescription>Vos modifications locales seront remplacées par la dernière version du serveur.</UiAlertDialogDescription></UiAlertDialogHeader>
              <UiAlertDialogFooter><UiAlertDialogCancel>Garder mes modifications</UiAlertDialogCancel><UiAlertDialogAction @click="refresh()">Recharger</UiAlertDialogAction></UiAlertDialogFooter>
            </UiAlertDialogContent>
          </UiAlertDialog>
        </div>
        <div class="flex items-center justify-between gap-4">
          <div class="min-w-0">
            <p class="flex items-center gap-2 text-xs font-medium sm:text-sm"><span class="size-1.5 shrink-0 rounded-full" :class="dirty ? 'bg-primary' : 'bg-muted-foreground'" />{{ dirty ? 'Modifications en attente' : saved.revision ? 'Tout est publié' : 'Première publication' }}</p>
            <p class="mt-1 text-[11px] text-muted-foreground">{{ publicationDate ? `Publié le ${publicationDate}` : 'Aucun contenu en ligne' }}</p>
          </div>
          <UiButton type="submit" form="editor" class="shrink-0 gap-3 px-5" :disabled="saving || (!dirty && !!saved.revision)">{{ saving ? 'Publication…' : 'Publier' }}<AppIcon name="arrow" /></UiButton>
        </div>
      </div>
    </div>
  </div>
</template>
