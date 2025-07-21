import { ref } from 'vue';
import { Info } from '@/common/types';
import { api } from '@/utils/api';

const info = ref<Info>();

export const useInfo = () => {
  const fetchInfo = async () => {
    info.value = await api<Info>('info');
  };

  return {
    info,
    fetchInfo,
  };
};
