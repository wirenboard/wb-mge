import { ref } from 'vue';
import { useAlerts } from '@/common/alert';
import { Settings } from '@/common/types';
import { api } from '@/utils/api';

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
  };

  const isChanged = (fields: string[]) => {
    return fields.some((field) => {
      const valA = (data.value as any)?.[field];
      const valB = (initData.value as any)?.[field];
      return !isDeepEqual(valA, valB);
    });
  };

  const updateSettings = async (body: any) => {
    isLoading.value = true;
    return api('settings', body).then(async (res: any) => {
      let invalidFieldsText = '';
      const invalidFields = Object.keys(res).filter(key => !res[key]);
      if (invalidFields.length) {
        invalidFields.forEach((field, i) => {
          invalidFieldsText += field;
          if (i !== invalidFields.length - 1) {
            invalidFieldsText += ', ';
          }
        });
        showAlert('invalid_fields', { type: 'success', interpolation: invalidFieldsText });
      } else {
        showAlert('data_updated', { type: 'success' });
      }
    }).finally(async () => {
      isLoading.value = false;
      await refresh();
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
