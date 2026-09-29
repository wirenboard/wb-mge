import ky from 'ky';
import { type Ref, ref, shallowRef } from 'vue';
import { useAlerts } from '@/common/alert';
import { hasSession } from '@/common/session';
import type {
  FpStatus,
  FpSuccessResponse,
  FpTgChatsResponse,
  FpUserRequest,
  FpUserResponse,
  FpUsersResponse,
} from '@/common/types';
import { isReconnecting } from '@/common/uptime';
import { ApiError, api } from '@/utils/api';

const STATUS_POLL_INTERVAL_MS = 1000;
const ENROLL_POLL_INTERVAL_MS = 500;
const IMAGE_TIMEOUT_MS = 5500;
const HTTP_NOT_FOUND = 404;

// Page-scoped: every call owns its own state, and the page starts it on mount and stops it on unmount.
// `fast` switches the period from 1 s to the 500 ms an enrollment is followed at.
export const useFpStatus = (fast: Ref<boolean>) => {
  const status = shallowRef<FpStatus>();
  const failed = ref(false);
  let timer: ReturnType<typeof setTimeout> | null = null;
  // Bumped by every start and stop, so an answer that lands after a stop is dropped instead of
  // resurrecting the loop.
  let generation = 0;

  const poll = async (run: number) => {
    const startedAt = performance.now();
    if (hasSession.value && !isReconnecting.value && document.visibilityState === 'visible') {
      try {
        const next = await api<FpStatus>('fp/status');
        if (run !== generation) {
          return;
        }
        status.value = next;
        failed.value = false;
      } catch (err) {
        if (run !== generation) {
          return;
        }
        if (!failed.value) {
          console.error('GET /fp/status failed', err);
        }
        failed.value = true;
      }
    }
    if (run !== generation) {
      return;
    }
    // A chain of timeouts rather than setInterval: the device serves one request at a time, and a
    // slow answer must not have the next poll queue up behind it.
    const interval = fast.value ? ENROLL_POLL_INTERVAL_MS : STATUS_POLL_INTERVAL_MS;
    const elapsed = performance.now() - startedAt;
    timer = setTimeout(() => poll(run), Math.max(0, interval - elapsed));
  };

  const stopPolling = () => {
    generation++;
    if (timer !== null) {
      clearTimeout(timer);
      timer = null;
    }
  };

  // Also the way to poll right now: an answer already in flight belongs to the old run and is dropped.
  const startPolling = () => {
    stopPolling();
    poll(generation);
  };

  return {
    status,
    failed,
    startPolling,
    stopPolling,
  };
};

// The image is raw bytes, not JSON, so it is fetched with ky directly under the same prefix api() uses.
// null means the device has no image (404).
export const fetchFpImage = async (): Promise<Uint8Array | null> => {
  const prefix = import.meta.env.DEV ? 'api/' : '';
  try {
    const buffer = await ky(`${prefix}fp/image`, { timeout: IMAGE_TIMEOUT_MS }).arrayBuffer();
    return new Uint8Array(buffer);
  } catch (err) {
    if ((err as { response?: Response }).response?.status === HTTP_NOT_FOUND) {
      return null;
    }
    throw err;
  }
};

// A refusal arrives either as an ApiError (HTTP 200 with success: false) or as ky's HTTPError that
// carries the same envelope with a 4xx status; both name the reason in "error". Anything else is a
// transport failure, whose message api() has already set ('timeout', 'connection_error', ...).
export const fpErrorReason = async (err: unknown): Promise<string> => {
  if (err instanceof ApiError && err.deviceError) {
    return err.deviceError;
  }
  const response = (err as { response?: Response }).response;
  if (response) {
    try {
      const body = await response.json();
      if (typeof body?.error === 'string') {
        return body.error;
      }
    } catch {
      // Not a JSON body: fall back to the error's own message.
    }
  }
  return (err as Error).message;
};

export const alertFpFailure = async (err: unknown) => {
  const { showAlert } = useAlerts();
  showAlert('fp_request_failed', { type: 'error', interpolation: await fpErrorReason(err) });
};

export const fetchFpUsers = () => api<FpUsersResponse>('fp/users');

export const saveFpUser = (json: FpUserRequest) => api<FpUserResponse>('fp/user', { method: 'POST', json });

export const deleteFpUser = (id: number) =>
  api<FpSuccessResponse>('fp/user/delete', { method: 'POST', json: { id } });

export const startFpEnroll = (userId: number) =>
  api<FpSuccessResponse>('fp/enroll', { method: 'POST', json: { action: 'start', user_id: userId } });

export const cancelFpEnroll = () => api<FpSuccessResponse>('fp/enroll', { method: 'POST', json: { action: 'cancel' } });

export const deleteFpFinger = (slot: number) =>
  api<FpSuccessResponse>('fp/finger/delete', { method: 'POST', json: { slot } });

export const fetchFpTgChats = () => api<FpTgChatsResponse>('fp/tg_chats');

export const addFpTgChat = (id: string, name: string) =>
  api<FpSuccessResponse>('fp/tg_chats', { method: 'POST', json: { action: 'add', id, name } });

export const removeFpTgChat = (id: string) =>
  api<FpSuccessResponse>('fp/tg_chats', { method: 'POST', json: { action: 'remove', id } });

export const clearFpTgChats = () => api<FpSuccessResponse>('fp/tg_chats', { method: 'POST', json: { action: 'clear' } });
