import ky, { type Options } from 'ky';
import { hasSession } from '@/common/session';

// The firmware's refusal envelope: {"success": false, "error": "..."} — ErrorResponse in
// openapi.yaml, produced by json_utils_send_error_status(). `success: false` never means "the
// request was carried out" on any endpoint, which is what makes it safe to reject on centrally.
export class ApiError extends Error {
  // The English sentence the device put in "error", when it sent one. Callers map it to a localized
  // message of their own; the Error's own message is what a console log or a bug report gets.
  readonly deviceError?: string;

  constructor(deviceError?: string) {
    super(deviceError ?? 'request_rejected');
    this.name = 'ApiError';
    this.deviceError = deviceError;
  }
}

// Only an explicit `false` counts as a refusal. A body without the field at all is left alone: that
// is every GET, plus POST /ports/N/mode and POST /ports/N/cache, which answer with their new state.
const rejectionEnvelope = (body: unknown): { error?: unknown } | null => {
  if (typeof body !== 'object' || body === null) {
    return null;
  }
  const envelope = body as { success?: unknown; error?: unknown };
  return envelope.success === false ? envelope : null;
};

export const api = async <T>(url: string, options: Options = {}): Promise<T> => {
  const prefix = import.meta.env.DEV ? 'api/' : '';
  let cfg: Options = { timeout: 5500, ...options };

  return ky(`${prefix}${url}`, cfg)
    .json<T>()
    .catch((err) => {
      if (err.name === 'TimeoutError') {
        throw new Error('timeout');
      }
      switch (err.response?.status) {
        case 404:
        case 500:
          throw new Error('connection_error');
        case 401:
          hasSession.value = false;
          throw Error('unauthorized');
      }
      throw err;
    })
    .then((body) => {
      // The envelope check belongs here and not in updateSettings(): it is the FIRMWARE's shape,
      // not the settings endpoint's, and most handlers pair it with a 4xx/5xx that the .catch()
      // above already turns into a rejection. Two send it with plain HTTP 200, so ky resolves them
      // as if they had succeeded: POST /settings when validation or an NVS write fails
      // (settings_manager.c) and POST /wifi_scan/start when a scan is already running
      // (wifi_scan.c). POST /settings alone has four callers — updateSettings() and three direct
      // posts in RegisterMap.vue — so a check inside updateSettings() would fix one of them and
      // leave the other three failing in silence.
      const rejected = rejectionEnvelope(body);
      if (rejected) {
        throw new ApiError(typeof rejected.error === 'string' ? rejected.error : undefined);
      }
      return body;
    });
};
