import { ref } from 'vue';
import { useAlerts } from '@/common/alert';
import { Settings } from '@/common/types';
import { api } from '@/utils/api';

export const useSettings = async () => {
  const isLoading = ref(false);
  const data = ref<Settings>();
  const initData = ref<Settings>();
  const { showAlert } = useAlerts();

  const refresh = async () => {
    data.value = await api<Settings>('settings');
    initData.value = { ...data.value };
  };

  await refresh();

  const isChanged = (fields: string[]) => {
    return fields.some((field) => (data.value as any)[field] !== (initData.value as any)[field]);
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
    isChanged,
    isLoading,
    updateSettings,
    refresh,
  };
};
