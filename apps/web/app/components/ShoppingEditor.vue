<script setup lang="ts">
import { LIMITS, shoppingLines } from '#shared/snapshot'

const text = defineModel<string>({ required: true })
defineProps<{ errors: string[] }>()
const count = computed(() => shoppingLines(text.value).length)
</script>

<template>
  <section aria-labelledby="shopping-title">
    <div class="mb-6 flex items-start justify-between gap-3">
      <div>
        <h2 id="shopping-title" class="text-xl font-medium tracking-tight">La liste de courses</h2>
        <p class="mt-1 text-sm text-muted-foreground">Une ligne, un article. Rien de plus.</p>
      </div>
      <UiAlertDialog>
        <UiAlertDialogTrigger as-child>
          <UiButton variant="ghost" size="icon" aria-label="Réinitialiser la liste" :disabled="!text.trim()"><AppIcon name="reset" /></UiButton>
        </UiAlertDialogTrigger>
        <UiAlertDialogContent>
          <UiAlertDialogHeader>
            <UiAlertDialogTitle>Vider la liste de courses ?</UiAlertDialogTitle>
            <UiAlertDialogDescription>La liste sera vidée dans le formulaire. Publiez ensuite pour appliquer ce changement.</UiAlertDialogDescription>
          </UiAlertDialogHeader>
          <UiAlertDialogFooter>
            <UiAlertDialogCancel>Conserver la liste</UiAlertDialogCancel>
            <UiAlertDialogAction @click="text = ''">Réinitialiser</UiAlertDialogAction>
          </UiAlertDialogFooter>
        </UiAlertDialogContent>
      </UiAlertDialog>
    </div>
    <div class="space-y-3">
      <UiLabel for="shopping">Articles à prévoir</UiLabel>
      <UiTextarea id="shopping" v-model="text" placeholder="Pain&#10;Lait&#10;Pommes" class="min-h-72 resize-y rounded-xl bg-card px-4 py-3 text-base leading-8 md:text-base" :aria-invalid="errors.length > 0" aria-describedby="shopping-help shopping-errors" />
      <div id="shopping-help" class="flex items-start justify-between gap-6 text-xs text-muted-foreground">
        <p>Libellés courts : {{ LIMITS.itemBytes }} octets UTF-8 par article.</p>
        <span class="shrink-0 font-mono" :class="count > LIMITS.shopping && 'text-destructive'">{{ count }} / {{ LIMITS.shopping }}</span>
      </div>
      <div id="shopping-errors" aria-live="polite"><p v-for="error in errors" :key="error" class="text-sm text-destructive">{{ error }}</p></div>
    </div>
    <p class="mt-7 flex items-center gap-2 text-xs text-muted-foreground"><span class="size-1.5 rounded-full bg-primary" />La liste reste en place jusqu’à votre prochaine publication.</p>
  </section>
</template>
