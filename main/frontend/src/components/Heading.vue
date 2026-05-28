<script setup lang="ts">
import { injectHead, useHead } from '@unhead/vue';
import { computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';

const props = defineProps<{ title: string; crumbs?: string }>();
const { data, isChanged } = useSettings();
const { locale } = useI18n();
const head = injectHead();

const prefix = computed(() => data.value?.hostname || 'WB-MGE v.3');

async function updatePageTitle() {
  useHead({
    title: `${prefix.value} — ${props.title}`,
  }, { head });
}

watch([() => locale.value, () => data.value?.hostname, () => isChanged(['hostname'])], () => {
  // prevent update page title before save
  if (!isChanged(['hostname'])) {
    updatePageTitle();
  }
}, { immediate: true });
</script>

<template>
  <header class="heading">
    <div class="heading-container">
      <div>
        <h1 class="heading-title">{{ title }}</h1>
        <div v-if="crumbs" class="heading-crumbs">{{ crumbs }}</div>
      </div>
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
  align-items: baseline;
  padding: 22px 32px 18px;
  border-bottom: 1px solid var(--border-color);
  background: var(--bg-surface);

  @media (max-width: 560px) {
    flex-direction: column;
    align-items: flex-start;
    padding: 16px;
  }
}

.heading-title {
  font-size: 20.8px; /* +0.8px for Roboto */
  font-weight: 600;
  letter-spacing: -0.01em;

  @media (max-width: 500px) {
    font-size: 18.8px; /* +0.8px for Roboto */
  }
}

.heading-crumbs {
  font-size: 12.8px; /* +0.8px for Roboto */
  color: var(--text-muted);
  margin-top: 4px;
  font-weight: 400;
}

.heading-container,
.heading-actions {
  display: flex;
  gap: 12px;
  height: min-content;
  flex-wrap: wrap;
  row-gap: 8px;
  align-items: center;
}

@media (max-width: 560px) {
  .heading-actions { width: 100%; }
}
</style>
