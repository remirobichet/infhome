<script setup lang="ts">
const colorMode = useColorMode()
const dark = computed(() => colorMode.value === 'dark')
</script>

<template>
  <ColorScheme tag="span">
    <UiButton variant="ghost" size="icon" :aria-label="dark ? 'Activer le thème clair' : 'Activer le thème sombre'" @click="colorMode.preference = dark ? 'light' : 'dark'">
      <AppIcon :name="dark ? 'sun' : 'moon'" />
    </UiButton>
    <template #placeholder><span class="block size-11" /></template>
  </ColorScheme>
</template>
