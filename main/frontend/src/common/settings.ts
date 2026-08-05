import { ref } from 'vue';
import { useAlerts } from '@/common/alert';
import type { DeepPartial, Settings, SettingsWarning, UpdateSettingsResponse } from '@/common/types';
import { ApiError, api } from '@/utils/api';
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

// Firmware error string -> i18n key, the mirror image of WARNING_MESSAGES above: here the device
// REFUSED the write and nothing was saved. Unlike a warning, a string we have no wording for is NOT
// shown raw — the firmware's other refusals are all "Failed to save <group> settings", which say
// the same thing to the user as the generic sentence does and say it in untranslated English.
const ERROR_MESSAGES: Record<string, string> = {
  'Invalid settings value': 'error_invalid_settings_value',
};

const settingsErrorMessage = (deviceError?: string): string =>
  (deviceError === undefined ? undefined : ERROR_MESSAGES[deviceError]) ?? 'settings_rejected';

// The device accepted the settings, only the read-back that follows the POST failed. Callers that
// roll their input back when updateSettings() rejects must not do that here: the value IS saved,
// and reverting the field would show the user the opposite of what the device holds.
export class SettingsRefreshError extends Error {
  // `cause` is declared on the class rather than taken from Error itself: the project compiles
  // against lib ES2021, where the constructor takes no options and Error has no `cause` at all.
  // Every browser the UI runs in has supported it since 2021, so this is a typing detail only.
  readonly cause?: unknown;

  constructor(message: string, options?: { cause?: unknown }) {
    super(message);
    this.cause = options?.cause;
  }
}

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
    let posted = false;
    return api<UpdateSettingsResponse>('settings', { method: 'POST', json })
      .then(async (response) => {
        posted = true;
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
      // The rejection handler is the second argument of .then() rather than a .catch() of its own
      // so that it sees the POST's failures only, and never a throw from the success branch above.
      }, (err) => {
        // The device refused the write: nothing was saved. api() has already turned a
        // {"success": false} body into an ApiError, so a refusal the firmware answered with HTTP
        // 200 arrives here exactly like a 4xx one. `posted` stays false, which is what makes the
        // .finally() below let THIS rejection through instead of a SettingsRefreshError: the
        // caller must roll its input back, which it must NOT do when only the read-back failed.
        if (err instanceof ApiError) {
          showAlert(settingsErrorMessage(err.deviceError), { type: 'error' });
        }
        throw err;
      })
      .finally(async () => {
        isLoading.value = false;
        try {
          await partialRefresh(savedKeys); // Only refresh the keys that were just saved
        } catch (err) {
          // A rejection out of .finally() replaces the outcome of the POST, so without this the
          // caller cannot tell "the device refused the setting" from "the setting was saved and the
          // re-read afterwards failed". The two need different words and different UI reactions.
          // When the POST itself failed the re-read almost certainly failed for the same reason, and
          // the original rejection is the one that describes what happened.
          if (posted) {
            // The original error is carried as the cause: its stack and its response object are the
            // only material a bug report about a failed read-back can be written from.
            throw new SettingsRefreshError((err as Error)?.message ?? 'settings re-read failed', { cause: err });
          }
        }
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
