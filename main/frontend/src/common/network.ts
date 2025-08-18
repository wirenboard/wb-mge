import { ref } from 'vue';
import type { WiFiNetwork, WifiScanResult } from '@/common/types';
import { api } from '@/utils/api';

const wifi = ref<WiFiNetwork[]>([]);
let intervalId: ReturnType<typeof setInterval> | null = null;

export const useWifi = () => {
  const isPolling = ref(false);

  const startScan = async () => {
    await api('wifi_scan/start', { method: 'POST' });
  };

  const fetchResults = async () => {
    wifi.value = await api<WifiScanResult>('wifi_scan/results').then(res => {
      if (res.scan_completed) {
        stopPolling();
      }

      if (res.networks) {
        return res.networks;
      } else {
        return [];
      }
    }).catch(() => []);
  };

  const startPolling = async () => {
    if (isPolling.value) return;
    isPolling.value = true;

    await startScan();
    await fetchResults();

    intervalId = setInterval(() => {
      fetchResults();
    }, 1000);
  };

  const stopPolling = () => {
    if (intervalId) {
      clearInterval(intervalId);
      intervalId = null;
    }
    isPolling.value = false;
  };

  return {
    wifi,
    isPolling,
    startPolling,
    stopPolling,
  };
};
