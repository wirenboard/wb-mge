import { ref } from 'vue';
import type { Session } from '@/common/types';
import { api } from '@/utils/api';

export const hasSession = ref(false);

export const useSession = async (): Promise<boolean> => {
  if (!hasSession.value) {
    hasSession.value = await api<Session>('session')
      .then(() => true)
      .catch(() => false);
  }

  return hasSession.value;
};
