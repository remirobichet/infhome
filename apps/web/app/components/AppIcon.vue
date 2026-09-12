<script setup lang="ts">
defineProps<{ name: 'sun' | 'moon' | 'logout' | 'plus' | 'trash' | 'arrow' | 'check' | 'reset' | 'lock' }>()
const paths = {
  sun: 'M12 3V1m0 22v-2M3 12H1m22 0h-2M5.6 5.6 4.2 4.2m15.6 15.6-1.4-1.4m0-12.8 1.4-1.4M4.2 19.8l1.4-1.4M17 12a5 5 0 1 1-10 0 5 5 0 0 1 10 0Z',
  moon: 'M20.9 13.4A9 9 0 0 1 10.6 3.1 9 9 0 1 0 20.9 13.4Z',
  logout: 'M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4m7 14 5-5-5-5m5 5H9',
  plus: 'M12 5v14M5 12h14',
  trash: 'M3 6h18M9 6V3h6v3M5 6l1 15h12l1-15M10 10v7m4-7v7',
  arrow: 'M5 12h14m-6-6 6 6-6 6',
  check: 'm5 12 4 4L19 6',
  reset: 'M3 10a9 9 0 1 1 2 8M3 4v6h6',
  lock: 'M7 11V7a5 5 0 0 1 10 0v4M5 11h14v10H5Zm7 4v2',
}
</script>

<template>
  <svg aria-hidden="true" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" class="size-4 shrink-0">
    <path :d="paths[name]" />
  </svg>
</template>
