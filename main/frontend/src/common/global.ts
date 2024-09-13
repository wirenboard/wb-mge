import { ref } from 'vue';

export const alertData = ref<{
  message: string;
  type: 'success' | 'error';
  withTranslation?: boolean;
} | null>(null);

export const firmwareVersion = ref<string>('');

