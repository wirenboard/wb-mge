import { ref } from 'vue';
import { alertData } from '@/common/global';
import { Settings } from '@/common/types';
import { api } from '@/utils/api';

export const useSettings = async () => {
  const data = ref<Settings>();
  const initData = ref<Settings>();

  const refresh = async () => {
    data.value = await api<Settings>('settings');
    initData.value = { ...data.value };
  };

  await refresh();

  const isChanged = (fields: string[]) => {
    return fields.some((field) => (data.value as any)[field] !== (initData.value as any)[field]);
  };

  const updateSettings = async (body: any, t: any) => {
    return api('settings', body).then((res: any) => {
      let errorText = t('invalid_fields');
      const invalidFields = Object.keys(res).filter(key => !res[key]);
      if (invalidFields.length) {
        invalidFields.forEach((field, i) => {
          errorText += t(field);
          if (i !== invalidFields.length - 1) {
            errorText += ', ';
          }
        });

        alertData.value = { type: 'error', message: errorText, withTranslation: true };
      } else {
        alertData.value = { type: 'success', message: 'data_updated', withTranslation: true };
      }
    }).finally(async () => {
      await refresh();
    });
  };

  return {
    data,
    isChanged,
    updateSettings,
    refresh,
  };
};
