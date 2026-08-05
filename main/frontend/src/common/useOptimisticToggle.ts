import { ref, computed, watch } from 'vue';
import { useInfo } from '@/common/info';

interface OptimisticToggleOptions {
  // Read the real boolean state from the globally-polled info ref.
  derive: () => boolean;
  // Optional hook invoked when the toggle action throws (e.g. show an alert).
  onError?: (e: unknown) => void;
}

/**
 * Shared optimistic-toggle state machine for boolean controls backed by the globally-polled
 * `info` ref. Consolidates the subtle invariant duplicated across TcpGateway / RegisterMap:
 *  - an `optimistic` override (null = derive real value from info; boolean = show until next poll),
 *  - an `inFlight` guard against concurrent/double-click toggles,
 *  - revert-on-error + an error hook,
 *  - an info-watch that clears the override ONLY when idle (otherwise a poll arriving before the
 *    firmware applied the change flickers the UI back to the old state).
 */
export function useOptimisticToggle(opts: OptimisticToggleOptions) {
  const { info, fetchInfo } = useInfo();
  const optimistic = ref<boolean | null>(null);
  const inFlight = ref(false);

  // Optimistic override wins; otherwise derive from polled info. Use a null check (NOT ||) so
  // an explicit optimistic `false` is honored.
  const value = computed<boolean>(() => (optimistic.value !== null ? optimistic.value : opts.derive()));

  // Run a toggle action. `action` receives the captured pre-toggle value.
  // applyOptimistic=false is for actions that must NOT flip the displayed value
  // (e.g. RegisterMap.resetMap: keep cache shown ON while it toggles off→on).
  async function run(action: (wasEnabled: boolean) => Promise<void>, applyOptimistic = true): Promise<void> {
    if (inFlight.value) return; // concurrency / double-click guard
    inFlight.value = true;
    const wasEnabled = value.value; // capture BEFORE applying optimistic override
    if (applyOptimistic) optimistic.value = !wasEnabled;
    try {
      await action(wasEnabled);
    } catch (e) {
      if (applyOptimistic) optimistic.value = null; // revert on failure
      opts.onError?.(e);
    } finally {
      inFlight.value = false;
      fetchInfo('low').catch(() => {}); // refresh sidebar/state immediately
    }
  }

  // Clear the optimistic override on each info refresh, but ONLY when idle — otherwise a poll
  // arriving before the firmware applied the change would flicker the UI back.
  watch(() => info.value, () => {
    if (!inFlight.value) optimistic.value = null;
  });

  return { value, inFlight, optimistic, run };
}
