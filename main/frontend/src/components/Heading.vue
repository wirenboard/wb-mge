<script setup lang="ts">
import { injectHead, useHead } from '@unhead/vue';
import { computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';

const props = defineProps<{ title: string }>();
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
      <h1 class="heading-title">{{ title }}</h1>
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

  @media (max-width: 560px) {
    flex-direction: column;
    align-items: flex-start;
  }

  @media (max-width: 500px) {
    font-size: 14px;
    margin-bottom: 6px;
  }
}

.heading-title {
  @media (max-width: 500px) {
    font-size: 24px;
  }
}

.heading-container,
.heading-actions {
  display: flex;
  gap: 12px;
  height: min-content;
}
</style>
