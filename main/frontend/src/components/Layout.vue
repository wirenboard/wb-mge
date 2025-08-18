<template>
  <div class="container">
    <Sidebar />
    <main class="content">
      <slot />
    </main>
    <AlertsWrapper />
  </div>
</template>

<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useRoute, useRouter } from 'vue-router';
import Sidebar from '@/components/Sidebar.vue';
import AlertsWrapper from '@/components/AlertsWrapper.vue';

const { t } = useI18n();
const route = useRoute();
const router = useRouter();

router.afterEach(() => {
  document.title = `${t(route.name as string)} — WB-MGE v.3`;
});
</script>

<style scoped>
.container {
  display: flex;

  @media (max-width: 680px) {
    flex-direction: column;
    gap: 12px;
  }
}
.content {
  overflow-y: auto;
  width: 100vw;
  padding: 18px 24px 24px;
  height: 100%;
  max-height: calc(100dvh - 42px);

  @media (max-width: 680px) {
    width: calc(100% - 24px);
    padding: 0 12px 12px;
    max-height: calc(100dvh - 84px);
  }
}
</style>
