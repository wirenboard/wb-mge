import { ref } from 'vue';
import { api } from '@/utils/api';

export const hasSession = ref(false);

export const useSession = async () => {
  if (!hasSession.value) {
    hasSession.value = await api('session')
      .then((res: any) => res?.status !== 401)
      .catch(() => false);
  }

  return hasSession.value;
};
