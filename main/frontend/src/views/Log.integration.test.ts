/**
 * Integration tests for Log.vue, the access log page. The status poll (useFpStatus) is the real one;
 * the device (api) is stubbed and answers GET /fp/status and GET /fp/log from the test.
 *
 * LOG-I-001 — the source column: Fingerprint, MQTT, Telegram, and "Telegram · <chat>" when the
 *              record names the chat the door was opened from.
 * LOG-I-002 — every language keeps both parts of "Telegram · <chat>".
 * LOG-I-003 — a source or a result this page does not know yet is shown as the device sent it.
 * LOG-I-004 — the result pill: Granted ok, Denied danger, Not recognized muted.
 * LOG-I-005 — no user and no clock: a dash in both columns.
 * LOG-I-006 — the log is read on the first status and re-read only when last_event.seq moves.
 * LOG-I-007 — an empty log says so.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import { hasSession } from '@/common/session';
import { messages } from '@/i18n/messages';
import { api } from '@/utils/api';
import type { FpLogEntry, FpStatus } from '@/common/types';

vi.mock('@/utils/api', () => ({
  ApiError: class ApiError extends Error {
    deviceError?: string;
  },
  api: vi.fn(),
}));

const STATUS_POLL_MS = 1000;

const makeStatus = (lastSeq: number | null): FpStatus => ({
  sensor: { online: true, capacity: 200, used: 3 },
  users_rev: 1,
  enroll: { state: 'idle', user_id: null, step: 0, steps: 5, slot: null, error: null },
  last_event: lastSeq === null ? null : {
    seq: lastSeq, uptime_s: 10, source: 'fingerprint', result: 'granted',
    user_id: 1, user_name: 'Anna', score: 120, chat: null,
  },
  image: null,
  lock: { on: false, online: true },
  telegram: { enabled: false, online: false, error: null },
});

const makeEntry = (seq: number, patch: Partial<FpLogEntry> = {}): FpLogEntry => ({
  seq,
  time: 1767225600, // 2026-01-01 00:00:00 UTC
  source: 'fingerprint',
  result: 'granted',
  user_id: 1,
  user_name: 'Anna',
  score: 120,
  chat: null,
  ...patch,
});

let status: FpStatus;
let entries: FpLogEntry[];
let logRequests: number;

const i18n = createI18n({ legacy: false, locale: 'en', messages, missingWarn: false, fallbackWarn: false });

const mountLog = async () => {
  const { default: Log } = await import('@/views/Log.vue');
  const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/', component: { template: '<div/>' } }] });
  const wrapper = mount(Log, {
    global: {
      plugins: [i18n, router],
      stubs: { Layout: { template: '<div><slot /></div>' }, Heading: true },
    },
  });
  await flushPromises();
  return wrapper;
};

// The cells of one column, top to bottom.
const column = (wrapper: Awaited<ReturnType<typeof mountLog>>, index: number) =>
  wrapper.findAll('tr.log-row').map((row) => row.findAll('td')[index].text());

const SOURCE_COLUMN = 2;
const RESULT_COLUMN = 3;

beforeEach(() => {
  hasSession.value = true;
  i18n.global.locale.value = 'en';
  status = makeStatus(5);
  entries = [];
  logRequests = 0;
  vi.mocked(api).mockReset().mockImplementation(async (path: string) => {
    if (path === 'fp/status') {
      // A new object on every answer, as from JSON on the wire.
      return structuredClone(status);
    }
    if (path === 'fp/log') {
      logRequests++;
      return { entries };
    }
    throw new Error(`unexpected request ${path}`);
  });
});

afterEach(() => {
  vi.useRealTimers();
  hasSession.value = false;
});

describe('Log.vue — the access log', () => {
  it('LOG-I-001: names the source, and the Telegram chat the door was opened from', async () => {
    entries = [
      makeEntry(5, { source: 'telegram', chat: 'Office', user_id: null, user_name: null }),
      makeEntry(4, { source: 'telegram', chat: '-1001234567890', user_id: null, user_name: null }),
      makeEntry(3, { source: 'telegram', chat: null }),
      makeEntry(2, { source: 'mqtt', user_id: null, user_name: null }),
      makeEntry(1, { source: 'fingerprint' }),
    ];
    const wrapper = await mountLog();

    expect(column(wrapper, SOURCE_COLUMN)).toEqual([
      'Telegram · Office',
      'Telegram · -1001234567890',
      'Telegram',
      'MQTT',
      'Fingerprint',
    ]);

    wrapper.unmount();
  });

  it('LOG-I-002: every language keeps the source and the chat', async () => {
    entries = [makeEntry(1, { source: 'telegram', chat: 'Офис' })];
    for (const locale of ['en', 'ru', 'kk', 'it', 'de']) {
      i18n.global.locale.value = locale as typeof i18n.global.locale.value;
      const wrapper = await mountLog();
      expect(column(wrapper, SOURCE_COLUMN), locale).toEqual(['Telegram · Офис']);
      wrapper.unmount();
    }
  });

  it('LOG-I-003: a source or a result this page does not know is shown as sent', async () => {
    entries = [makeEntry(1, { source: 'nfc' as FpLogEntry['source'], result: 'expired' as FpLogEntry['result'] })];
    const wrapper = await mountLog();

    expect(column(wrapper, SOURCE_COLUMN)).toEqual(['nfc']);
    expect(column(wrapper, RESULT_COLUMN)).toEqual(['expired']);

    wrapper.unmount();
  });

  it('LOG-I-004: the result pill carries the outcome and its colour', async () => {
    entries = [
      makeEntry(3, { result: 'granted' }),
      makeEntry(2, { result: 'denied' }),
      makeEntry(1, { result: 'unknown', user_id: null, user_name: null }),
    ];
    const wrapper = await mountLog();

    const pills = wrapper.findAll('tr.log-row .pill');
    expect(pills.map((pill) => pill.text())).toEqual(['Granted', 'Denied', 'Not recognized']);
    expect(pills[0].classes()).toContain('ok');
    expect(pills[1].classes()).toContain('danger');
    expect(pills[2].classes()).toContain('muted');

    wrapper.unmount();
  });

  it('LOG-I-005: no user and no clock read as a dash', async () => {
    entries = [makeEntry(1, { time: null, user_id: null, user_name: null, result: 'unknown' })];
    const wrapper = await mountLog();

    expect(column(wrapper, 0)).toEqual(['—']);
    expect(column(wrapper, 1)).toEqual(['—']);

    wrapper.unmount();
  });

  it('LOG-I-006: the log is re-read when an attempt moves last_event.seq, and only then', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    entries = [makeEntry(5)];
    const wrapper = await mountLog();
    expect(logRequests).toBe(1);

    // The same last event: a new status object, but nothing to re-read.
    await vi.advanceTimersByTimeAsync(STATUS_POLL_MS);
    await flushPromises();
    expect(logRequests).toBe(1);

    status = makeStatus(6);
    entries = [makeEntry(6, { source: 'mqtt', user_id: null, user_name: null }), makeEntry(5)];
    await vi.advanceTimersByTimeAsync(STATUS_POLL_MS);
    await flushPromises();
    expect(logRequests).toBe(2);
    expect(column(wrapper, SOURCE_COLUMN)).toEqual(['MQTT', 'Fingerprint']);

    wrapper.unmount();
  });

  it('LOG-I-006: a device with no event since boot still gets its log read', async () => {
    status = makeStatus(null);
    entries = [makeEntry(9)];
    const wrapper = await mountLog();

    expect(logRequests).toBe(1);
    expect(wrapper.findAll('tr.log-row')).toHaveLength(1);

    wrapper.unmount();
  });

  it('LOG-I-007: an empty log says so', async () => {
    const wrapper = await mountLog();

    expect(wrapper.text()).toContain('No opening attempts logged yet');
    expect(wrapper.find('table').exists()).toBe(false);

    wrapper.unmount();
  });
});
