import { ref } from 'vue';
import { useRouter, useRoute } from 'vue-router';
import { useFirmware } from '@/common/firmware';
import type { Uptime } from '@/common/types';
import { api } from '@/utils/api';

let intervalId: ReturnType<typeof setInterval> | null = null;
const uptime = ref<Uptime>({ days: 0, hours: 0, minutes: 0, seconds: 0 });
const isReconnecting = ref(false);

export const useUptime = () => {
  const router = useRouter();
  const route = useRoute();
  const { isUpdating } = useFirmware();

  const checkUptime = async () => {
    try {
      uptime.value = await api<Uptime>('uptime', { priority: 'low' });
      isReconnecting.value = false;

      if (route.name === 'login') {
        await router.push({ name: 'dashboard' });
      }
    } catch (err: any) {
      if (err.message === 'unauthorized') {
        isReconnecting.value = false;
        await router.push({ name: 'login' });
      } else {
        isReconnecting.value = true;
      }
    }
  };

  const startPolling = async () => {
    if (intervalId) return;

    await checkUptime();

    intervalId = setInterval(() => {
      if (document.visibilityState === 'visible' && !isUpdating.value) {
        checkUptime();
      }
    }, 10000);
  };

  return {
    uptime,
    isReconnecting,
    startPolling,
  };
};
