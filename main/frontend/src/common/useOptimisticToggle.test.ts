/**
 * Unit tests for the useOptimisticToggle composable.
 *
 * Covers the optimistic-toggle state machine shared by TcpGateway / RegisterMap:
 *  - value derives from info when the optimistic override is null;
 *  - the optimistic override wins (including an explicit `false`);
 *  - run() sets inFlight, applies the optimistic flip, calls action with the captured
 *    pre-toggle value, and on finally clears inFlight + calls fetchInfo('low');
 *  - on throw it reverts the optimistic override and invokes onError;
 *  - the concurrency guard: a second run while one is in flight is a no-op;
 *  - applyOptimistic=false does not flip the displayed value (but still guards/refreshes);
 *  - the info-watch clears the optimistic override only when idle (not while inFlight).
 *
 * @/common/info is mocked the same way the view integration tests mock it: a controllable
 * `infoRef` + a `fetchInfo` vi.fn. The composable imports useInfo() from there.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { ref, nextTick } from 'vue';

// Controllable info ref + fetchInfo spy, shared with the mocked useInfo() below.
const infoRef = ref<unknown>(undefined);
const fetchInfoMock = vi.fn().mockResolvedValue(undefined);

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: fetchInfoMock }),
}));

import { useOptimisticToggle } from './useOptimisticToggle';

/** A deferred promise so a test can hold an action in flight and resolve/reject it on demand. */
function deferred<T = void>() {
  let resolve!: (v: T) => void;
  let reject!: (e: unknown) => void;
  const promise = new Promise<T>((res, rej) => {
    resolve = res;
    reject = rej;
  });
  return { promise, resolve, reject };
}

beforeEach(() => {
  infoRef.value = undefined;
  fetchInfoMock.mockClear();
  fetchInfoMock.mockResolvedValue(undefined);
});

describe('useOptimisticToggle — value derivation', () => {
  it('derives from info when the optimistic override is null', () => {
    // derive() must read a reactive source so the `value` computed re-evaluates — in production
    // it reads info.value; here a ref stands in for that reactive backend state.
    const backend = ref(false);
    const t = useOptimisticToggle({ derive: () => backend.value });
    expect(t.value.value).toBe(false);
    backend.value = true;
    expect(t.value.value).toBe(true);
  });

  it('optimistic override wins over the derived value', () => {
    const t = useOptimisticToggle({ derive: () => false });
    t.optimistic.value = true;
    expect(t.value.value).toBe(true);
  });

  it('honors an explicit optimistic `false` (null-check, not ||)', () => {
    // derive() returns true, but an explicit optimistic false must override it.
    const t = useOptimisticToggle({ derive: () => true });
    t.optimistic.value = false;
    expect(t.value.value).toBe(false);
  });
});

describe('useOptimisticToggle — run() happy path', () => {
  it('sets inFlight, applies optimistic flip, passes captured wasEnabled, clears + fetchInfo on finally', async () => {
    const t = useOptimisticToggle({ derive: () => false }); // starts OFF
    const d = deferred();
    let observed: { wasEnabled: boolean; inFlightDuring: boolean; optimisticDuring: boolean | null } | null = null;

    const p = t.run(async (wasEnabled) => {
      observed = {
        wasEnabled,
        inFlightDuring: t.inFlight.value,
        optimisticDuring: t.optimistic.value,
      };
      await d.promise;
    });

    // While the action is pending: inFlight true, optimistic flipped to !wasEnabled (true).
    expect(observed).not.toBeNull();
    expect(observed!.wasEnabled).toBe(false); // captured the pre-toggle value
    expect(observed!.inFlightDuring).toBe(true);
    expect(observed!.optimisticDuring).toBe(true); // optimistic flip applied before the action ran
    expect(t.value.value).toBe(true); // displayed value reflects the optimistic flip
    expect(fetchInfoMock).not.toHaveBeenCalled(); // not until the action settles

    d.resolve();
    await p;

    expect(t.inFlight.value).toBe(false); // cleared on finally
    expect(fetchInfoMock).toHaveBeenCalledWith('low'); // refresh on finally
    // optimistic is NOT cleared by run() itself on success — that happens on the next info poll.
    expect(t.optimistic.value).toBe(true);
  });
});

describe('useOptimisticToggle — error path', () => {
  it('reverts the optimistic override and calls onError on throw', async () => {
    const onError = vi.fn();
    const t = useOptimisticToggle({ derive: () => false, onError });
    const err = new Error('boom');

    await t.run(async () => {
      throw err;
    });

    expect(t.optimistic.value).toBeNull(); // reverted on failure
    expect(t.value.value).toBe(false); // back to the derived value
    expect(onError).toHaveBeenCalledWith(err);
    expect(t.inFlight.value).toBe(false);
    expect(fetchInfoMock).toHaveBeenCalledWith('low'); // finally still refreshes
  });
});

describe('useOptimisticToggle — concurrency guard', () => {
  it('a second run while one is in flight is a no-op', async () => {
    const t = useOptimisticToggle({ derive: () => false });
    const d = deferred();
    const first = vi.fn(async () => {
 await d.promise;
});
    const second = vi.fn(async () => {});

    const p = t.run(first);
    // Second call while the first is still in flight must not invoke its action.
    await t.run(second);
    expect(second).not.toHaveBeenCalled();
    expect(first).toHaveBeenCalledTimes(1);

    d.resolve();
    await p;

    // After the first completes, a fresh run proceeds normally.
    await t.run(second);
    expect(second).toHaveBeenCalledTimes(1);
  });
});

describe('useOptimisticToggle — applyOptimistic=false', () => {
  it('does not flip the displayed value but still guards and refreshes', async () => {
    const t = useOptimisticToggle({ derive: () => true }); // displayed ON
    const d = deferred();
    let optimisticDuring: boolean | null = 'sentinel' as unknown as boolean | null;

    const p = t.run(async () => {
      optimisticDuring = t.optimistic.value;
      await d.promise;
    }, /* applyOptimistic */ false);

    // Optimistic override is left untouched (null) so the displayed value stays derived (ON).
    expect(optimisticDuring).toBeNull();
    expect(t.value.value).toBe(true);
    expect(t.inFlight.value).toBe(true); // still guards concurrency

    d.resolve();
    await p;

    expect(t.inFlight.value).toBe(false);
    expect(fetchInfoMock).toHaveBeenCalledWith('low');
  });
});

describe('useOptimisticToggle — info-watch clears override only when idle', () => {
  it('clears the optimistic override on an info refresh when no run is in flight', async () => {
    const t = useOptimisticToggle({ derive: () => false });
    t.optimistic.value = true;

    // Simulate an info poll (new Info object).
    infoRef.value = { tick: 1 };
    await nextTick();

    expect(t.optimistic.value).toBeNull();
  });

  it('does NOT clear the optimistic override while a run is in flight', async () => {
    const t = useOptimisticToggle({ derive: () => false });
    const d = deferred();

    const p = t.run(async () => {
      await d.promise;
    });
    // run() flipped optimistic to true and inFlight is true.
    expect(t.optimistic.value).toBe(true);

    // A poll arriving mid-flight must NOT clear the override (would flicker the UI back).
    infoRef.value = { tick: 1 };
    await nextTick();
    expect(t.optimistic.value).toBe(true);

    d.resolve();
    await p;
  });
});
