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

// Only an explicit `false` counts as a refusal. A body without the field at all is left alone:
// that is every GET, and any POST that answers with its new state instead of an envelope.
const rejectionEnvelope = (body: unknown): { error?: unknown } | null => {
  if (typeof body !== 'object' || body === null) {
    return null;
  }
  const envelope = body as { success?: unknown; error?: unknown };
  return envelope.success === false ? envelope : null;
};

const DEFAULT_TIMEOUT_MS = 5500;

// In development the device is reached through the Vite proxy mounted at /api.
const requestUrl = (url: string): string => `${import.meta.env.DEV ? 'api/' : ''}${url}`;

const mapRequestError = (err: any): never => {
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
};

export const api = async <T>(url: string, options: Options = {}): Promise<T> => {
  let cfg: Options = { timeout: DEFAULT_TIMEOUT_MS, ...options };

  return ky(requestUrl(url), cfg)
    .json<T>()
    .catch(mapRequestError)
    .then((body) => {
      // The envelope check belongs here and not in updateSettings(): it is the FIRMWARE's shape,
      // not the settings endpoint's, and most handlers pair it with a 4xx/5xx that the .catch()
      // above already turns into a rejection. Two send it with plain HTTP 200, so ky resolves them
      // as if they had succeeded: POST /settings when validation or an NVS write fails
      // (settings_manager.c) and POST /wifi_scan/start when a scan is already running
      // (wifi_scan.c). Keeping the check here covers every caller of every endpoint at once,
      // instead of one wrapper at a time while the rest fail in silence.
      const rejected = rejectionEnvelope(body);
      if (rejected) {
        throw new ApiError(typeof rejected.error === 'string' ? rejected.error : undefined);
      }
      return body;
    });
};

// api() for endpoints that answer with raw bytes. There is no envelope check: a refusal from such an
// endpoint always comes with a 4xx/5xx status, which the shared mapping above already rejects on.
// The headers come along for endpoints that report side values in them.
export const apiBinary = async (url: string, options: Options = {}): Promise<{ body: ArrayBuffer; headers: Headers }> =>
  ky(requestUrl(url), { timeout: DEFAULT_TIMEOUT_MS, ...options })
    .then(async (response) => ({ body: await response.arrayBuffer(), headers: response.headers }))
    .catch(mapRequestError);
