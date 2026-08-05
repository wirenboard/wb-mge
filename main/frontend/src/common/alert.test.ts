/**
 * Unit tests for useAlerts() / showAlert() from src/common/alert.ts.
 *
 * The point of interest is WHICH alert a timeout removes. showAlert() used to drop alerts[0] —
 * the OLDEST alert — which is the alert that armed the timer only as long as every alert lives
 * exactly as long as every other one. Any caller passing its own `timeout` breaks that: the FIFO
 * order and the expiry order no longer agree, and a short-lived toast expires a longer-lived
 * alert ahead of its time.
 *
 * alert.ts keeps module-scoped state (the shared `alerts` array), so each test resets the module
 * registry with vi.resetModules() and re-imports a fresh copy — the isolation strategy
 * settings.test.ts and session.test.ts use.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

const DEFAULT_TIMEOUT_MS = 4000;
const LONG_TIMEOUT_MS = 10000;

describe('useAlerts', () => {
  beforeEach(() => {
    vi.resetModules();
    vi.useFakeTimers();
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('AL-001: an alert is shown and removed when its own default timeout expires', async () => {
    const { useAlerts } = await import('@/common/alert');
    const { alerts, showAlert } = useAlerts();

    showAlert('saved', { type: 'success' });

    expect(alerts).toHaveLength(1);
    expect(alerts[0]).toMatchObject({ message: 'saved', type: 'success' });

    vi.advanceTimersByTime(DEFAULT_TIMEOUT_MS - 1);
    expect(alerts).toHaveLength(1); // not a millisecond early

    vi.advanceTimersByTime(1);
    expect(alerts).toHaveLength(0);
  });

  it('AL-002: a short-lived alert does not expire a longer-lived one shown before it', async () => {
    const { useAlerts } = await import('@/common/alert');
    const { alerts, showAlert } = useAlerts();

    showAlert('long_lived', { timeout: LONG_TIMEOUT_MS });

    // A second later an ordinary 4 s toast arrives.
    vi.advanceTimersByTime(1000);
    showAlert('data_updated', { type: 'success' });

    // The toast's timer fires first. It must take the TOAST, not the head of the array.
    vi.advanceTimersByTime(DEFAULT_TIMEOUT_MS);
    expect(alerts).toHaveLength(1);
    expect(alerts[0]).toMatchObject({ message: 'long_lived' });

    // The long-lived alert stays up until its own 10 s are over: 1000 + 4000 elapsed, 5000 to go.
    vi.advanceTimersByTime(LONG_TIMEOUT_MS - DEFAULT_TIMEOUT_MS - 1000 - 1);
    expect(alerts).toHaveLength(1);

    vi.advanceTimersByTime(1);
    expect(alerts).toHaveLength(0);
  });

  it('AL-003: each of several alerts is removed by its own timer, whatever the order', async () => {
    const { useAlerts } = await import('@/common/alert');
    const { alerts, showAlert } = useAlerts();

    showAlert('slow', { timeout: 9000 });
    showAlert('fast', { timeout: 1000 });
    showAlert('medium', { timeout: 5000 });

    expect(alerts.map((a) => a.message)).toEqual(['slow', 'fast', 'medium']);

    vi.advanceTimersByTime(1000);
    expect(alerts.map((a) => a.message)).toEqual(['slow', 'medium']);

    vi.advanceTimersByTime(4000);
    expect(alerts.map((a) => a.message)).toEqual(['slow']);

    vi.advanceTimersByTime(4000);
    expect(alerts).toHaveLength(0);
  });

  it('AL-004: identical messages shown twice expire one per timer, not both at once', async () => {
    const { useAlerts } = await import('@/common/alert');
    const { alerts, showAlert } = useAlerts();

    // Two alerts with equal contents are still two distinct entries: the removal goes by identity,
    // so the first timer must take exactly one of them — not both, and not the wrong one.
    showAlert('data_updated', { type: 'success' });
    vi.advanceTimersByTime(2000);
    showAlert('data_updated', { type: 'success' });

    expect(alerts).toHaveLength(2);

    vi.advanceTimersByTime(2000); // the first alert's 4 s are up, the second still has 2 s
    expect(alerts).toHaveLength(1);

    vi.advanceTimersByTime(2000);
    expect(alerts).toHaveLength(0);
  });
});
