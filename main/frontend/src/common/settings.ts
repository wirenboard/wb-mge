import { ref } from 'vue';
import { useAlerts } from '@/common/alert';
import type { DeepPartial, Settings, SettingsWarning, UpdateSettingsResponse } from '@/common/types';
import { api } from '@/utils/api';
import { setHostname } from '@/common/hostname';

// A warning stays up longer than the "saved" toast: it is the only place the user learns that the
// accepted settings leave a service unable to bind its TCP port.
const WARNING_ALERT_TIMEOUT_MS = 10000;

// Firmware warning code -> i18n key. A code we do not know (older/newer firmware) falls back to the
// English message the firmware sent, which is still far better than silence.
const WARNING_MESSAGES: Record<string, string> = {
  port_collision: 'warning_port_collision',
};

const warningAlertMessage = (warning: SettingsWarning): string =>
  WARNING_MESSAGES[warning.code] ?? warning.message;

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
      .then(async (response) => {
        // The settings WERE saved, so this is not an error — but a green "data updated" would hide
        // the fact that the firmware flagged the result (e.g. two services on one TCP port, one of
        // which will not come up). Show the warnings instead of the success toast.
        const warnings = response?.warnings ?? [];
        if (warnings.length > 0) {
          warnings.forEach((warning) => {
            showAlert(warningAlertMessage(warning), { type: 'warning', timeout: WARNING_ALERT_TIMEOUT_MS });
          });
          return;
        }
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
    partialRefresh,
    updateSettings,
    refresh,
  };
};
