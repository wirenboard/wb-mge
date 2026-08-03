import { ref } from 'vue';
import type { UpdateResponse } from '@/common/types';
import { api } from '@/utils/api';

const isUpdating = ref(false);

export const useFirmware = () => {
  const update = async (file: File) => {
    isUpdating.value = true;
    await api<UpdateResponse>('update', { method: 'POST', body: file, timeout: 30000 })
      .catch((err) => {
        // 409: the device already has a firmware image written and is about to reboot into it, so
        // it refuses to start over. Retrying only makes sense after the reboot.
        if (err.response?.status === 409) {
          throw new Error('update_in_progress');
        }
        throw err;
      })
      .finally(() => {
        isUpdating.value = false;
      });
  };

  return {
    update,
    isUpdating,
  };
};
