import { ref, watch } from 'vue';
import type { Session } from '@/common/types';
import { api } from '@/utils/api';

export const hasSession = ref(false);

// Cached in-flight/result of the /session probe. Reused so the cold-load guard
// chain (`/` -> redirect -> `/login`) does ONE request instead of two.
let probe: Promise<boolean> | null = null;

// Invalidate the cached probe whenever auth is cleared (a 401 in api() or logout
// flips hasSession to false), so the next guard genuinely re-checks the server
// instead of returning a stale result. Sync flush is required: with the default
// (pre) flush the reset is async, leaving a window where an expired session
// (api() just set hasSession=false) could still let a stale resolved-true probe
// pass a route guard. Sync flush nulls the probe in the same tick.
watch(hasSession, (authed) => {
  if (!authed) {
    probe = null;
  }
}, { flush: 'sync' });

export const useSession = async (): Promise<boolean> => {
  if (hasSession.value) return true;
  if (!probe) {
    probe = api<Session>('session')
      .then(() => {
        hasSession.value = true;
        return true;
      })
      .catch(() => false);
  }

  return probe;
};
