/**
 * Unit tests for useSession() / hasSession from src/common/session.ts (P3).
 *
 * Covers the request-dedup behaviour that collapses the cold-load guard chain
 * (`/` -> redirect -> `/login`) into a SINGLE /session probe, plus the cached-
 * probe invalidation driven by the watch(hasSession) reset.
 *
 * session.ts keeps module-scoped state (the cached `probe`), so each test resets
 * the module registry with vi.resetModules() and re-imports a fresh copy, the same
 * isolation strategy settings.test.ts uses.
 */

import { beforeEach, describe, expect, it, vi } from 'vitest';

describe('useSession', () => {
  // Re-create module-level state (hasSession ref + cached probe) fresh per test.
  beforeEach(() => {
    vi.resetModules();
  });

  it('P3-001: a logged-out double-call dedups to ONE api(session) request', async () => {
    // Unauthed: the /session probe rejects (api throws on 401).
    const apiMock = vi.fn().mockRejectedValue(new Error('unauthorized'));
    vi.doMock('@/utils/api', () => ({ api: apiMock }));

    const { useSession } = await import('@/common/session');

    // Two sequential awaits simulate the `/`->`/login` guard chain.
    const first = await useSession();
    const second = await useSession();

    expect(first).toBe(false);
    expect(second).toBe(false);
    // The cached probe must be reused, so api('session') runs exactly once.
    expect(apiMock).toHaveBeenCalledTimes(1);
    expect(apiMock).toHaveBeenCalledWith('session');
  });

  it('P3-002: once authed via hasSession, useSession returns true without another api call', async () => {
    const apiMock = vi.fn().mockRejectedValue(new Error('unauthorized'));
    vi.doMock('@/utils/api', () => ({ api: apiMock }));

    const { useSession, hasSession } = await import('@/common/session');

    // Probe resolves false first (one api call).
    expect(await useSession()).toBe(false);
    expect(apiMock).toHaveBeenCalledTimes(1);

    // Flip auth on (e.g. after a successful login) — the early-return short-circuits.
    hasSession.value = true;
    expect(await useSession()).toBe(true);
    // No second probe: useSession returned true off hasSession alone.
    expect(apiMock).toHaveBeenCalledTimes(1);
  });

  it('P3-003: an authed probe caches true and is not re-fetched', async () => {
    // Authed: the /session probe resolves; useSession sets hasSession=true.
    const apiMock = vi.fn().mockResolvedValue({});
    vi.doMock('@/utils/api', () => ({ api: apiMock }));

    const { useSession, hasSession } = await import('@/common/session');

    expect(await useSession()).toBe(true);
    expect(hasSession.value).toBe(true);
    expect(apiMock).toHaveBeenCalledTimes(1);

    // Subsequent call short-circuits on hasSession — no extra probe.
    expect(await useSession()).toBe(true);
    expect(apiMock).toHaveBeenCalledTimes(1);
  });

  it('P3-004: flipping hasSession back to false (sync flush) invalidates the cached probe', async () => {
    const apiMock = vi.fn().mockResolvedValue({});
    vi.doMock('@/utils/api', () => ({ api: apiMock }));

    const { useSession, hasSession } = await import('@/common/session');
    const { nextTick } = await import('vue');

    // First probe resolves true and caches the resolved promise.
    expect(await useSession()).toBe(true);
    expect(apiMock).toHaveBeenCalledTimes(1);

    // Simulate an expired session: api() flips hasSession=false. The watch runs with
    // { flush: 'sync' }, so the cached probe is nulled in the same tick.
    hasSession.value = false;
    await nextTick(); // belt-and-suspenders; the sync watch already ran

    // The next call must genuinely re-probe the server rather than reuse the stale
    // resolved-true promise.
    expect(await useSession()).toBe(true);
    expect(apiMock).toHaveBeenCalledTimes(2);
  });
});
