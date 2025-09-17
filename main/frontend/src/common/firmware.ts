import { ref } from 'vue';
import type { UpdateResponse } from '@/common/types';
import { api } from '@/utils/api';

const isUpdating = ref(false);

export const useFirmware = () => {
  const update = async (file: File) => {
    isUpdating.value = true;
    await api<UpdateResponse>('update', { method: 'POST', body: file, timeout: 30000 })
      .finally(() => {
        isUpdating.value = false;
      });
  };

  return {
    update,
    isUpdating,
  };
};
