import { ref } from 'vue';
import { api } from '@/utils/api';

const hostname = ref<string>('');

export const useHostname = () => {
  const fetchHostname = async () => {
    try {
      const data = await api<{ hostname: string }>('hostname');
      hostname.value = data.hostname;
    } catch {
      // ignore — hostname is optional display info
    }
  };

  return { hostname, fetchHostname };
};
