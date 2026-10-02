/**
 * Integration tests for the "Last event" card of Access.vue. The status poll (useFpStatus) is the
 * real one; the device is stubbed: api answers GET /fp/status, /fp/users and /uptime, and ky (which
 * fetchFpImage uses for the raw image) answers GET /fp/image.
 *
 * ACC-I-001 — the source row: Fingerprint, MQTT, Telegram, and "Telegram · <chat>".
 * ACC-I-002 — the result pill and the user of the event; nobody reads as a dash.
 * ACC-I-003 — the image is shown beside the event it was taken for, and only beside that one: a
 *              scan with no image of its own does not borrow the previous one.
 * ACC-I-004 — no event since boot: no pill, the "no events" note, no image.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import ky from 'ky';
import { hasSession } from '@/common/session';
import { messages } from '@/i18n/messages';
import { api } from '@/utils/api';
import type { FpEvent, FpStatus } from '@/common/types';

vi.mock('@/utils/api', () => ({
  ApiError: class ApiError extends Error {
    deviceError?: string;
  },
  api: vi.fn(),
}));

vi.mock('ky', () => ({
  default: vi.fn(),
}));

const STATUS_POLL_MS = 1000;
const IMAGE_WIDTH = 4;
const IMAGE_HEIGHT = 2;

const makeEvent = (seq: number, patch: Partial<FpEvent> = {}): FpEvent => ({
  seq,
  uptime_s: 50,
  source: 'fingerprint',
  result: 'granted',
  user_id: 1,
  user_name: 'Anna',
  score: 120,
  chat: null,
  ...patch,
});

const makeStatus = (lastEvent: FpEvent | null, image: FpStatus['image'] = null): FpStatus => ({
  sensor: { online: true, capacity: 200, used: 3 },
  users_rev: 1,
  enroll: { state: 'idle', user_id: null, step: 0, steps: 5, slot: null, error: null },
  last_event: lastEvent,
  image,
  lock: { on: false, online: true },
  telegram: { enabled: false, online: false, error: null },
});

const makeImage = (seq: number, eventSeq: number): FpStatus['image'] => ({
  seq, event_seq: eventSeq, width: IMAGE_WIDTH, height: IMAGE_HEIGHT,
});

let status: FpStatus;
let imageRequests: number;

const i18n = createI18n({ legacy: false, locale: 'en', messages, missingWarn: false, fallbackWarn: false });

const mountAccess = async () => {
  const { default: Access } = await import('@/views/Access.vue');
  const router = createRouter({ history: createMemoryHistory(), routes: [{ path: '/', component: { template: '<div/>' } }] });
  await router.push('/');
  await router.isReady();
  const wrapper = mount(Access, {
    global: {
      plugins: [i18n, router],
      stubs: { Layout: { template: '<div><slot /></div>' }, Heading: true },
    },
  });
  await flushPromises();
  return wrapper;
};

type AccessWrapper = Awaited<ReturnType<typeof mountAccess>>;

// The "Last event" card: the second card of the top row.
const eventCard = (wrapper: AccessWrapper) => wrapper.findAll('.grid-2 section.card')[1];

const sourceRow = (wrapper: AccessWrapper) => {
  const text = eventCard(wrapper).text();
  const match = /Source\s*(.*?)\s*User/.exec(text);
  return match ? match[1] : null;
};

const nextPoll = async () => {
  await vi.advanceTimersByTimeAsync(STATUS_POLL_MS);
  await flushPromises();
};

beforeEach(() => {
  hasSession.value = true;
  status = makeStatus(null);
  imageRequests = 0;
  vi.mocked(api).mockReset().mockImplementation(async (path: string) => {
    if (path === 'fp/status') {
      // A new object on every answer, as from JSON on the wire.
      return structuredClone(status);
    }
    if (path === 'fp/users') {
      return { users: [], max_users: 100, max_fingers: 10 };
    }
    if (path === 'uptime') {
      return { days: 0, hours: 0, minutes: 1, seconds: 0 };
    }
    throw new Error(`unexpected request ${path}`);
  });
  vi.mocked(ky).mockReset().mockImplementation((() => {
    imageRequests++;
    return { arrayBuffer: async () => new Uint8Array(IMAGE_WIDTH * IMAGE_HEIGHT).buffer };
  }) as unknown as typeof ky);
});

afterEach(() => {
  vi.useRealTimers();
  hasSession.value = false;
});

describe('Access.vue — the last event', () => {
  it('ACC-I-001: names the source, and the Telegram chat the door was opened from', async () => {
    const cases: [Partial<FpEvent>, string][] = [
      [{ source: 'telegram', chat: 'Office', user_id: null, user_name: null }, 'Telegram · Office'],
      [{ source: 'telegram', chat: '555' }, 'Telegram · 555'],
      [{ source: 'telegram', chat: null }, 'Telegram'],
      [{ source: 'mqtt', user_id: null, user_name: null }, 'MQTT'],
      [{ source: 'fingerprint' }, 'Fingerprint'],
      [{ source: 'nfc' as FpEvent['source'] }, 'nfc'],
    ];
    for (const [patch, label] of cases) {
      status = makeStatus(makeEvent(1, patch));
      const wrapper = await mountAccess();
      expect(sourceRow(wrapper), label).toBe(label);
      wrapper.unmount();
    }
  });

  it('ACC-I-002: the pill carries the outcome; an attempt by nobody shows a dash for the user', async () => {
    status = makeStatus(makeEvent(1, { source: 'mqtt', result: 'granted', user_id: null, user_name: null }));
    const wrapper = await mountAccess();

    const pill = eventCard(wrapper).find('.card-header .pill');
    expect(pill.text()).toBe('Granted');
    expect(pill.classes()).toContain('ok');
    expect(eventCard(wrapper).text()).toMatch(/User\s*—/);

    wrapper.unmount();
  });

  it('ACC-I-003: the image is shown beside its own event and dropped when a later one has none', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    status = makeStatus(makeEvent(7), makeImage(3, 7));
    const wrapper = await mountAccess();

    expect(imageRequests).toBe(1);
    const canvas = eventCard(wrapper).find('canvas');
    expect(canvas.exists()).toBe(true);
    expect(canvas.attributes('width')).toBe(String(IMAGE_WIDTH));
    expect(canvas.attributes('height')).toBe(String(IMAGE_HEIGHT));

    // A Telegram opening: an event with no image; the old image (of event 7) stays on the device.
    status = makeStatus(makeEvent(8, { source: 'telegram', chat: 'Office' }), makeImage(3, 7));
    await nextPoll();
    expect(sourceRow(wrapper)).toBe('Telegram · Office');
    expect(eventCard(wrapper).find('canvas').exists()).toBe(false);
    // The same image is not downloaded again.
    expect(imageRequests).toBe(1);

    // The next scan brings its own image.
    status = makeStatus(makeEvent(9), makeImage(4, 9));
    await nextPoll();
    expect(imageRequests).toBe(2);
    expect(eventCard(wrapper).find('canvas').exists()).toBe(true);

    wrapper.unmount();
  });

  it('ACC-I-003: an enrollment take is never shown as the image of an event', async () => {
    status = makeStatus(makeEvent(7), makeImage(5, 0));
    const wrapper = await mountAccess();

    expect(eventCard(wrapper).find('canvas').exists()).toBe(false);

    wrapper.unmount();
  });

  it('ACC-I-004: no event since boot: a note instead of the event, no pill, no image', async () => {
    status = makeStatus(null, makeImage(5, 0));
    const wrapper = await mountAccess();

    expect(eventCard(wrapper).text()).not.toContain('Source');
    expect(eventCard(wrapper).find('.pill').exists()).toBe(false);
    expect(eventCard(wrapper).find('canvas').exists()).toBe(false);

    wrapper.unmount();
  });
});
