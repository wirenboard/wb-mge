import ky from 'ky';
import { useAlerts } from '@/common/alert';
import { hasSession } from '@/common/session';

export const api = async <T>(url: string, body: any = null, isFile?: boolean): Promise<T> => {
  const prefix = import.meta.env.DEV ? 'api/' : '';
  const cfg = body ? { method: 'POST', body: isFile ? body : JSON.stringify(body) } : undefined;
  const { showAlert } = useAlerts();

  return ky(`${prefix}${url}`, cfg)
    .json<T>()
    // @ts-ignore
    .then(async (res) => res.text ? JSON.parse(await res.text()) : res)
    .catch(({ response }) => {
      switch (response.status) {
        case 500:
          showAlert('connection_error');
          throw new Error('connection_error');
        case 401:
          hasSession.value = false;
      }
      return response;
    });
};
