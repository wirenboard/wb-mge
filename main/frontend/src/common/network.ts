import { ref } from 'vue';
import type { WiFiNetwork, WifiScanResponce, WifiScanStartResponce } from '@/common/types';
import { api } from '@/utils/api';

let intervalId: ReturnType<typeof setInterval> | null = null;

export const useWifi = () => {
  const wifi = ref<WiFiNetwork[]>([]);
  const isPolling = ref(false);

  const startScan = async () => {
    await api<WifiScanStartResponce>('wifi_scan/start', { method: 'POST', timeout: 15000 });
  };

  const fetchResults = async () => {
    try {
      const res = await api<WifiScanResponce>('wifi_scan/results');

      if (res.scan_completed || res.error || (!res.scan_completed && !res.scan_completed)) {
        stopPolling();
      }

      wifi.value = res.networks ?? [];
    } catch (e) {
      stopPolling();
    }
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
