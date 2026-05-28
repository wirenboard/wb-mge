import { ref } from 'vue';
import { useAlerts } from '@/common/alert';
import type { DeepPartial, Settings, UpdateSettingsResponse } from '@/common/types';
import { api } from '@/utils/api';
import { setHostname } from '@/common/hostname';

const isDeepEqual = (a: any, b: any): boolean => {
  if (a === b) return true;

  if (typeof a !== typeof b || a === null || b === null) return false;

  if (typeof a === 'object') {
    if (Array.isArray(a) !== Array.isArray(b)) return false;

    const keysA = Object.keys(a);
    const keysB = Object.keys(b);
    if (keysA.length !== keysB.length) return false;

    return keysA.every((key) => isDeepEqual(a[key], b[key]));
  }

  return false;
};

const data = ref<Settings>();
const initData = ref<Settings>();

export const useSettings = () => {
  const isLoading = ref(false);
  const { showAlert } = useAlerts();

  const refresh = async () => {
    data.value = await api<Settings>('settings');
    initData.value = JSON.parse(JSON.stringify(data.value));
    if (data.value?.hostname) {
      setHostname(data.value.hostname);
    }
  };

  // Fetches fresh settings from the server but only applies the specified keys,
  // leaving other keys (potentially dirty) in data and initData completely untouched.
  const partialRefresh = async (savedKeys: Array<keyof Settings>) => {
    const serverData = await api<Settings>('settings');
    if (data.value && initData.value && serverData) {
      for (const key of savedKeys) {
        (data.value as any)[key] = (serverData as any)[key];
        // Deep-clone the server value so data and initData do not share the same object reference;
        // without this, editing data[key] would silently mutate initData[key] too, breaking isChanged().
        (initData.value as any)[key] = JSON.parse(JSON.stringify((serverData as any)[key]));
      }
    }
    if (serverData?.hostname && savedKeys.includes('hostname')) {
      setHostname(serverData.hostname);
    }
  };

  const isChanged = (fields: string[]) => {
    return fields.some((field) => {
      const valA = (data.value as any)?.[field];
      const valB = (initData.value as any)?.[field];
      return !isDeepEqual(valA, valB);
    });
  };

  const updateSettings = async (json: DeepPartial<Settings>) => {
    isLoading.value = true;
    const savedKeys = Object.keys(json) as Array<keyof Settings>; // capture before async
    return api<UpdateSettingsResponse>('settings', { method: 'POST', json })
      .then(async () => {
        showAlert('data_updated', { type: 'success' });
      })
      .finally(async () => {
        isLoading.value = false;
        await partialRefresh(savedKeys); // Only refresh the keys that were just saved
      });
  };

  return {
    data,
    initData,
    isChanged,
    isLoading,
    updateSettings,
    refresh,
  };
};
