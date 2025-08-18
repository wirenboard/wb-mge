import { ref } from 'vue';
import { Uptime } from '@/common/types';
import { api } from '@/utils/api';

const interval = ref<ReturnType<typeof setInterval> | null>(null);
const uptime = ref<Uptime>({ days: 0, hours: 0, minutes: 0, seconds: 0 });
const isReconnecting = ref(false);

export const useUptime = () => {
  const checkUptime = async () => {
    try {
      uptime.value = await api<Uptime>('uptime');
      isReconnecting.value = false;
    } catch (e) {
      isReconnecting.value = true;
    }
  };

  const startPolling = async () => {
    if (interval.value) return;

    await checkUptime();

    interval.value = setInterval(() => {
      checkUptime();
    }, 10000);
  };

  return {
    uptime,
    isReconnecting,
    startPolling,
  };
};
