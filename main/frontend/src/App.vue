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
import { watch } from 'vue';
import PageProgress from '@/components/PageProgress.vue';
import ReconnectModal from '@/components/ReconnectModal.vue';
import { useUptime } from '@/common/uptime';
import { useInfo } from '@/common/info';
import { hasSession } from '@/common/session';

const { isReconnecting, startPolling: startUptimePolling, stopPolling: stopUptimePolling } = useUptime();
// Poll /uptime and /info globally so the Sidebar always has up-to-date port status,
// but only while authenticated so the login screen does not churn out 401s.
const { startPolling: startInfoPolling, stopPolling: stopInfoPolling } = useInfo();

watch(hasSession, (authed) => {
  if (authed) {
    startUptimePolling();
    startInfoPolling();
  } else {
    stopUptimePolling();
    stopInfoPolling();
  }
}, { immediate: true });
</script>
