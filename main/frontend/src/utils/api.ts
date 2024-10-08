import ky from 'ky';
import { alertData } from '@/common/global';
import { hasSession } from '@/common/session';

export const api = async <T>(url: string, body: any = null, isFile?: boolean): Promise<T> => {
  const prefix = import.meta.env.DEV ? 'api/' : '';
  const cfg = body ? { method: 'POST', body: isFile ? body : JSON.stringify(body) } : undefined;

  return ky(`${prefix}${url}`, cfg)
    .json<T>()
    .catch(({ response }) => {
      switch (response.status) {
        case 500:
          alertData.value = { message: 'connection_error', type: 'error', withTranslation: true };
          throw new Error('connection_error');
        case 401:
          hasSession.value = false;
      }
      return response;
    });
};
