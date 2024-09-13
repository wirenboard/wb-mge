<script setup lang="ts">
defineProps<{ title: string; infoLink?: string }>();
</script>

<template>
  <header class="heading">
    <div class="heading-title">
      <h1>{{ title }}</h1>
      <a v-if="infoLink" :href="infoLink" target="_blank">
        <svg class="heading-icon" width="46.08" height="46.08" viewBox="0 0 512 512" fill="currentColor" stroke-width="32">
          <path fill="none" stroke="currentColor" d="M248 64C146.39 64 64 146.39 64 248s82.39 184 184 184 184-82.39 184-184S349.61 64 248 64z" />
          <path fill="none" stroke="currentColor" d="M220 220h32v116" />
          <path fill="none" stroke="currentColor" d="M208 340h88" />
          <path d="M248 130a26 26 0 1026 26 26 26 0 00-26-26z" />
        </svg>
      </a>
    </div>

    <div class="heading-actions">
      <slot />
    </div>
  </header>
</template>

<style scoped>
.heading {
  display: flex;
  gap: 12px;
  justify-content: space-between;
  margin-bottom: 24px;
  align-items: center;
}

.heading-title,
.heading-actions {
  display: flex;
  gap: 12px;
  height: min-content;
}

.heading-icon {
  color: var(--text-color);
  width: 20px;
}
</style>
