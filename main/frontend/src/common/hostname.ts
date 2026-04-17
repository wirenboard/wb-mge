import { ref } from 'vue';
import { api } from '@/utils/api';

const hostname = ref<string>('');
let fetched = false;

export const useHostname = () => {
  if (!fetched) {
    fetched = true;
    api<{ hostname: string }>('hostname')
      .then(data => {
        hostname.value = data.hostname;
      })
      .catch(() => {
        fetched = false;
      });
  }
  return { hostname };
};
