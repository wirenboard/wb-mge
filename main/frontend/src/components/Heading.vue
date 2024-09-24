<script setup lang="ts">
import InfoIcon from '@/assets/infoIcon.svg?component';

defineProps<{ title: string; infoLink?: string }>();
</script>

<template>
  <header class="heading">
    <div class="heading-title">
      <h1>{{ title }}</h1>
      <a v-if="infoLink" :href="infoLink" target="_blank">
        <InfoIcon class="heading-icon" />
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
  height: 34px;
}
</style>
