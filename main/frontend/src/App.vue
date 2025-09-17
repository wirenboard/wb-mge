<template>
  <PageProgress />
  <RouterView v-slot="{ Component }">
    <template v-if="Component">
      <Suspense>
        <component :is="Component"></component>
      </Suspense>
    </template>
  </RouterView>
  <ReconnectModal v-if="isReconnecting" />
</template>

<script setup lang="ts">
import { onMounted } from 'vue';
import PageProgress from '@/components/PageProgress.vue';
import ReconnectModal from '@/components/ReconnectModal.vue';
import { useUptime } from '@/common/uptime';

const { isReconnecting, startPolling } = useUptime();

onMounted(() => {
  startPolling();
});
</script>
