import { ref } from 'vue';
import type { Info } from '@/common/types';
import { useUptime } from '@/common/uptime';
import { api } from '@/utils/api';

const info = ref<Info>();
let intervalId: ReturnType<typeof setInterval> | null = null;

export const useInfo = () => {
  const { isReconnecting } = useUptime();
  const fetchInfo = async (priority: 'low' | 'high') => {
    info.value = await api<Info>('info', { timeout: 5000, priority });
  };

  const startPolling = async () => {
    if (intervalId) return;

    await fetchInfo('high');

    intervalId = setInterval(() => {
      if (!isReconnecting.value) {
        fetchInfo('low');
      }
    }, 5000);
  };

  const stopPolling = () => {
    if (intervalId) {
      clearInterval(intervalId);
      intervalId = null;
    }
    intervalId = null;
  };

  return {
    info,
    fetchInfo,
    startPolling,
    stopPolling,
  };
};
