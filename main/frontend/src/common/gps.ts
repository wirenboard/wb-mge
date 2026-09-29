import { ref, shallowRef } from 'vue';
import { hasSession } from '@/common/session';
import type { GpsStatus } from '@/common/types';
import { isReconnecting } from '@/common/uptime';
import { api } from '@/utils/api';

const GPS_POLL_INTERVAL_MS = 1000;
const US_PER_MS = 1000;

// One GET /gps answer and the two instants around it. The device read its clock somewhere between
// sending and receiving, so half the round trip is both the best guess of how old that reading is
// at receipt and the bound on how wrong the guess can be.
export interface GpsSample {
  status: GpsStatus;
  sentAtPerfMs: number;
  receivedAtPerfMs: number;
  receivedAtWallMs: number;
}

export interface DeviceClock {
  // Device time, unix ms, at the performance.now() instant `perfMs`.
  unixMs: number;
  perfMs: number;
  // Device minus this computer, the NTP sign convention: what to add to this computer's clock.
  offsetMs: number;
  uncertaintyMs: number;
}

export const deviceClockOf = (sample: GpsSample): DeviceClock | null => {
  const unixUs = sample.status.time.unix_us;
  if (unixUs === null) {
    return null;
  }
  const halfRttMs = (sample.receivedAtPerfMs - sample.sentAtPerfMs) / 2;
  const unixMs = (unixUs / US_PER_MS) + halfRttMs;
  return {
    unixMs,
    perfMs: sample.receivedAtPerfMs,
    offsetMs: unixMs - sample.receivedAtWallMs,
    uncertaintyMs: halfRttMs,
  };
};

// Page-scoped: every call owns its own state, and the page starts it on mount and stops it on unmount.
export const useGps = () => {
  const sample = shallowRef<GpsSample>();
  const failed = ref(false);
  let timer: ReturnType<typeof setTimeout> | null = null;
  // Bumped by every start and stop, so an answer that lands after a stop is dropped instead of
  // resurrecting the loop.
  let generation = 0;

  const poll = async (run: number) => {
    const startedAt = performance.now();
    // The same gate as the /info poller, plus the session: after a 401 there is nothing to ask for.
    if (hasSession.value && !isReconnecting.value && document.visibilityState === 'visible') {
      try {
        // Default priority on purpose: a 'low' fetch may be held back by the browser, and that
        // wait would widen the round trip the clock offset is bounded by.
        const status = await api<GpsStatus>('gps');
        const receivedAtPerfMs = performance.now();
        const receivedAtWallMs = Date.now();
        if (run !== generation) {
          return;
        }
        sample.value = { status, sentAtPerfMs: startedAt, receivedAtPerfMs, receivedAtWallMs };
        failed.value = false;
      } catch (err) {
        if (run !== generation) {
          return;
        }
        if (!failed.value) {
          console.error('GET /gps failed', err);
        }
        failed.value = true;
      }
    }
    if (run !== generation) {
      return;
    }
    // A chain of timeouts rather than setInterval: the device serves one request at a time, and a
    // slow answer must not have the next poll queue up behind it.
    const elapsed = performance.now() - startedAt;
    timer = setTimeout(() => poll(run), Math.max(0, GPS_POLL_INTERVAL_MS - elapsed));
  };

  const stopPolling = () => {
    generation++;
    if (timer !== null) {
      clearTimeout(timer);
      timer = null;
    }
  };

  const startPolling = () => {
    stopPolling();
    poll(generation);
  };

  return {
    sample,
    failed,
    startPolling,
    stopPolling,
  };
};
