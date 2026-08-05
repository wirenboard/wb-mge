import { beforeEach, describe, expect, it, vi } from 'vitest';

/**
 * Unit tests for api() — the single place every HTTP call to the device goes through.
 *
 * The firmware answers a refused request with the ErrorResponse envelope of openapi.yaml,
 * {"success": false, "error": "..."}. Most handlers pair it with a 4xx/5xx that ky rejects on by
 * itself, but two send it under a plain HTTP 200 — POST /settings when validation or an NVS write
 * fails, and POST /wifi_scan/start when a scan is already running — and those used to resolve here
 * as if the device had done what it was asked.
 *
 * API-001 — a 200 carrying {"success": false} rejects with ApiError and keeps the device's text.
 * API-002 — a refusal without an "error" string still rejects; deviceError is simply absent.
 * API-003 — {"success": true} resolves with the body, so a clean save is untouched.
 * API-004 — a body with no "success" field at all (every GET) resolves unchanged.
 * API-005 — a non-object body is not mistaken for an envelope.
 * API-006 — 401 still clears the session and rejects with 'unauthorized'.
 * API-007 — 404/500 still map to 'connection_error' and a TimeoutError to 'timeout'.
 */

// Loads api.ts against a ky stub. Both are re-imported per test so hasSession starts fresh.
const loadApi = async (respond: (url: string, options: any) => Promise<unknown>) => {
  const kyMock = vi.fn((url: string, options: any) => ({ json: () => respond(url, options) }));
  vi.doMock('ky', () => ({ default: kyMock }));

  const { ApiError, api } = await import('@/utils/api');
  const { hasSession } = await import('@/common/session');
  return { ApiError, api, hasSession, kyMock };
};

const resolving = (body: unknown) => () => Promise.resolve(body);
const rejecting = (err: unknown) => () => Promise.reject(err);

describe('api', () => {
  beforeEach(() => {
    vi.resetModules();
  });

  it('API-001: a 200 body with success:false rejects with the error the device sent', async () => {
    const { ApiError, api } = await loadApi(resolving({ success: false, error: 'Invalid settings value' }));

    const rejection = await api('settings', { method: 'POST' }).catch((err) => err);

    expect(rejection).toBeInstanceOf(ApiError);
    // The raw firmware string is kept so the caller can map it to a localized message; it is also
    // what a console log or a bug report about an unknown refusal has to go on.
    expect((rejection as InstanceType<typeof ApiError>).deviceError).toBe('Invalid settings value');
    expect((rejection as Error).message).toBe('Invalid settings value');
  });

  it('API-002: a refusal without an error string still rejects', async () => {
    const { ApiError, api } = await loadApi(resolving({ success: false }));

    const rejection = await api('settings', { method: 'POST' }).catch((err) => err);

    expect(rejection).toBeInstanceOf(ApiError);
    expect((rejection as InstanceType<typeof ApiError>).deviceError).toBeUndefined();
  });

  it('API-003: success:true resolves with the body', async () => {
    const body = { success: true, warnings: [{ code: 'port_collision', message: 'x' }] };
    const { api } = await loadApi(resolving(body));

    await expect(api('settings', { method: 'POST' })).resolves.toEqual(body);
  });

  it('API-004: a body without a success field is left alone', async () => {
    // GET /settings, GET /info, POST /ports/1/mode — none of them carry the envelope, and none of
    // them may start rejecting because of a check written for the ones that do.
    const { api } = await loadApi(resolving({ hostname: 'wb-mge', web_port: 80 }));

    await expect(api('settings')).resolves.toEqual({ hostname: 'wb-mge', web_port: 80 });
  });

  it('API-005: a non-object body is not read as an envelope', async () => {
    const { api } = await loadApi(resolving(''));

    await expect(api('ports/1/cache', { method: 'POST' })).resolves.toBe('');
  });

  it('API-006: a 401 clears the session and rejects with unauthorized', async () => {
    const { api, hasSession } = await loadApi(rejecting({ response: { status: 401 } }));
    hasSession.value = true;

    await expect(api('settings')).rejects.toThrow('unauthorized');
    expect(hasSession.value).toBe(false);
  });

  it('API-007: transport failures keep their existing messages', async () => {
    for (const status of [404, 500]) {
      vi.resetModules();
      const { api } = await loadApi(rejecting({ response: { status } }));
      await expect(api('settings')).rejects.toThrow('connection_error');
    }

    vi.resetModules();
    const { api } = await loadApi(rejecting({ name: 'TimeoutError' }));
    await expect(api('settings')).rejects.toThrow('timeout');
  });
});
