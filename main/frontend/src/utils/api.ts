import ky, { type Options } from 'ky';
import { hasSession } from '@/common/session';

export const api = async <T>(url: string, options: Options = {}): Promise<T> => {
  const prefix = import.meta.env.DEV ? 'api/' : '';
  let cfg: Options = { timeout: 5500, ...options };

  return ky(`${prefix}${url}`, cfg)
    .json<T>()
    .catch((err) => {
      if (err.name === 'TimeoutError') {
        return err.response;
      }
      switch (err.response?.status) {
        case 404:
        case 500:
          throw new Error('connection_error');
        case 401:
          hasSession.value = false;
          throw Error('unauthorized');
      }
      return err.response;
    });
};
