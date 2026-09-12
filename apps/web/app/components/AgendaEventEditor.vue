<script setup lang="ts">
import type { AgendaEvent } from '#shared/snapshot'

const event = defineModel<AgendaEvent>({ required: true })
defineProps<{ index: number, errors: Record<string, string> }>()
defineEmits<{ remove: [] }>()
</script>

<template>
  <fieldset class="min-w-0 rounded-xl border bg-card p-4 sm:p-5">
    <legend class="sr-only">Événement {{ index + 1 }}</legend>
    <div class="mb-3 flex items-center justify-between">
      <span class="eyebrow">RENDEZ-VOUS / {{ String(index + 1).padStart(2, '0') }}</span>
      <UiButton type="button" variant="ghost" size="icon" :aria-label="`Supprimer l’événement ${event.title || index + 1}`" @click="$emit('remove')"><AppIcon name="trash" /></UiButton>
    </div>
    <div class="space-y-4">
      <div class="space-y-2">
        <UiLabel :for="`title-${event.id}`">Titre</UiLabel>
        <UiInput :id="`title-${event.id}`" v-model="event.title" placeholder="Un dîner, un départ, un rendez-vous…" maxlength="96" :aria-invalid="!!errors.title" :aria-describedby="errors.title ? `title-error-${event.id}` : undefined" />
        <p v-if="errors.title" :id="`title-error-${event.id}`" class="text-sm text-destructive">{{ errors.title }}</p>
      </div>
      <div class="grid grid-cols-1 gap-4 min-[380px]:grid-cols-2">
        <div class="min-w-0 space-y-2">
          <UiLabel :for="`start-${event.id}`">Date de début</UiLabel>
          <UiInput :id="`start-${event.id}`" v-model="event.startDate" type="date" min="2000-01-01" max="2099-12-31" :aria-invalid="!!errors.startDate" :aria-describedby="errors.startDate ? `start-error-${event.id}` : undefined" />
          <p v-if="errors.startDate" :id="`start-error-${event.id}`" class="text-sm text-destructive">{{ errors.startDate }}</p>
        </div>
        <div class="min-w-0 space-y-2">
          <UiLabel :for="`end-${event.id}`">Fin <span class="font-normal text-muted-foreground">(facultative)</span></UiLabel>
          <UiInput :id="`end-${event.id}`" :model-value="event.endDate ?? ''" type="date" :min="event.startDate || '2000-01-01'" max="2099-12-31" :aria-invalid="!!errors.endDate" :aria-describedby="errors.endDate ? `end-error-${event.id}` : undefined" @update:model-value="event.endDate = String($event) || null" />
          <p v-if="errors.endDate" :id="`end-error-${event.id}`" class="text-sm text-destructive">{{ errors.endDate }}</p>
        </div>
      </div>
      <div class="max-w-48 space-y-2">
        <UiLabel :for="`time-${event.id}`">Heure <span class="font-normal text-muted-foreground">(facultative)</span></UiLabel>
        <UiInput :id="`time-${event.id}`" :model-value="event.time ?? ''" type="time" :aria-invalid="!!errors.time" :aria-describedby="errors.time ? `time-error-${event.id}` : undefined" @update:model-value="event.time = String($event) || null" />
        <p v-if="errors.time" :id="`time-error-${event.id}`" class="text-sm text-destructive">{{ errors.time }}</p>
      </div>
    </div>
  </fieldset>
</template>
