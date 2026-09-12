<script setup lang="ts">
import { errorMessage } from '~/lib/errors'

useHead({ title: 'Connexion — Infhome' })
const auth = useAuth()
const username = ref('')
const password = ref('')
const pending = ref(false)
const error = ref('')

async function submit() {
  pending.value = true
  error.value = ''
  try {
    await auth.login(username.value, password.value)
    password.value = ''
    await navigateTo('/')
  }
  catch (cause) { error.value = errorMessage(cause, 'Connexion impossible. Vérifiez votre réseau puis réessayez.') }
  finally { pending.value = false }
}
</script>

<template>
  <div class="mx-auto flex min-h-dvh max-w-5xl flex-col px-5 sm:px-8">
    <header class="flex items-center justify-between border-b py-5">
      <BrandMark />
      <ThemeToggle />
    </header>
    <main id="main" class="grid flex-1 items-center gap-12 py-12 md:grid-cols-[1.1fr_1fr] md:gap-20">
      <div class="space-y-6">
        <p class="eyebrow">ESPACE PERSONNEL / 01</p>
        <h1 class="max-w-sm text-4xl font-medium leading-[1.12] tracking-tight sm:text-5xl">Un petit écran.<br><span class="text-muted-foreground">Votre quotidien.</span></h1>
        <p class="max-w-xs leading-relaxed text-muted-foreground">Les courses à prévoir, les rendez-vous à garder en tête. Tout commence ici.</p>
        <div class="flex items-center gap-4 text-muted-foreground/60" aria-hidden="true">
          <span class="size-3 border" /><span class="size-3 rounded-full border" /><span class="font-mono text-xl leading-none">×</span><span class="text-lg">△</span>
          <span class="h-px w-16 bg-border" />
        </div>
      </div>
      <section class="rounded-2xl border bg-card p-6 sm:p-8" aria-labelledby="login-title">
        <div class="mb-7 flex items-center justify-between">
          <h2 id="login-title" class="text-xl font-medium tracking-tight">Connexion</h2>
          <AppIcon name="lock" class="text-muted-foreground" />
        </div>
        <form class="space-y-5" :aria-busy="pending" @submit.prevent="submit">
          <div class="space-y-2">
            <UiLabel for="username">Identifiant</UiLabel>
            <UiInput id="username" v-model="username" name="username" autocomplete="username" autocapitalize="none" :spellcheck="false" maxlength="128" required :disabled="pending" />
          </div>
          <div class="space-y-2">
            <UiLabel for="password">Mot de passe</UiLabel>
            <UiInput id="password" v-model="password" name="password" type="password" autocomplete="current-password" maxlength="512" required :disabled="pending" />
          </div>
          <p v-if="error" role="alert" class="text-sm text-destructive">{{ error }}</p>
          <UiButton type="submit" class="w-full justify-between" :disabled="pending">
            {{ pending ? 'Connexion…' : 'Se connecter' }}<AppIcon name="arrow" />
          </UiButton>
        </form>
      </section>
    </main>
    <footer class="flex justify-between border-t py-5 font-mono text-[10px] uppercase tracking-widest text-muted-foreground">
      <span>Le quotidien, simplement.</span><span>Infhome / v1</span>
    </footer>
  </div>
</template>
