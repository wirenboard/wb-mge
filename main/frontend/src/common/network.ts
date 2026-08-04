import { ref } from 'vue';
import type { WiFiNetwork, WifiScanResponce, WifiScanStartResponce } from '@/common/types';
import { ApiError, api } from '@/utils/api';

let intervalId: ReturnType<typeof setInterval> | null = null;

export const useWifi = () => {
  const wifi = ref<WiFiNetwork[]>([]);
  const isPolling = ref(false);

  const startScan = async () => {
    // The firmware answers "a scan is already in progress" with HTTP 200 and {"success": false},
    // which api() surfaces as an ApiError. For us that is not a failure: a scan IS running, so the
    // results poll below has exactly what it needs, and letting the rejection out would leave
    // startPolling() with isPolling stuck true and no interval armed. Anything else — no session,
    // no device, a timeout — is a real failure and still propagates.
    await api<WifiScanStartResponce>('wifi_scan/start', { method: 'POST', timeout: 15000 })
      .catch((err) => {
        if (!(err instanceof ApiError)) {
          throw err;
        }
      });
  };

  const fetchResults = async () => {
    try {
      const res = await api<WifiScanResponce>('wifi_scan/results');

      if (res.error || (res.scan_completed && !res.scan_in_progress)) {
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
