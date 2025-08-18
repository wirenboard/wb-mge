import ky from 'ky';
import { hasSession } from '@/common/session';

export const api = async <T>(url: string, body: any = null, isFile?: boolean): Promise<T> => {
  const prefix = import.meta.env.DEV ? 'api/' : '';
  const cfg = body ? { method: 'POST', body: isFile ? body : JSON.stringify(body) } : { timeout: 3500 };

  return ky(`${prefix}${url}`, cfg)
    .json<T>()
    // @ts-ignore
    .then(async (res) => res.text ? JSON.parse(await res.text()) : res)
    .catch(({ response }) => {
      switch (response.status) {
        case 404:
        case 500:
          throw new Error('connection_error');
        case 401:
          hasSession.value = false;
      }
      return response;
    });
};
