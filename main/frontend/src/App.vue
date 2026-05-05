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
import { useInfo } from '@/common/info';

const { isReconnecting, startPolling: startUptimePolling } = useUptime();
// Start polling /info globally so the Sidebar always has up-to-date port status.
const { startPolling: startInfoPolling } = useInfo();

onMounted(() => {
  startUptimePolling();
  startInfoPolling();
});
</script>
