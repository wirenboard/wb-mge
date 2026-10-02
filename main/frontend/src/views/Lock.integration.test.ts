/**
 * Integration tests for the status pills and the relay switch of Lock.vue. The status poll
 * (useFpStatus) is the real one; the settings are stubbed, and api answers GET /fp/status, GET
 * /fp/tg_chats and POST /fp/lock from the test.
 *
 * LOCK-I-001 — before the first status answer neither card has a pill.
 * LOCK-I-002 — the relay pill: Online (ok) while the module answers, Offline (danger) otherwise.
 * LOCK-I-003 — the Telegram pill appears only once a token is set: Online (ok) or Offline (danger).
 * LOCK-I-004 — the pills follow the status as it changes.
 * LOCK-I-005 — the relay switch shows lock.on, posts the state asked for, and goes back to what
 *               the relay reports when the relay did not follow.
 * LOCK-I-006 — skipped, a known bug: the reason the bot is offline (telegram.error) is not shown.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import { hasSession } from '@/common/session';
import { messages } from '@/i18n/messages';
import { api } from '@/utils/api';
import type { FpStatus, Settings } from '@/common/types';

vi.mock('@/utils/api', () => ({
  ApiError: class ApiError extends Error {
    deviceError?: string;
  },
  api: vi.fn(),
}));

const settingsRef = ref<Partial<Settings>>({});

vi.mock('@/common/settings', () => ({
  SettingsRefreshError: class SettingsRefreshError extends Error {},
  useSettings: () => ({
    data: settingsRef,
    initData: settingsRef,
    isChanged: () => false,
    isLoading: ref(false),
    partialRefresh: vi.fn().mockResolvedValue(undefined),
    updateSettings: vi.fn().mockResolvedValue(undefined),
    refresh: vi.fn(),
  }),
}));

const STATUS_POLL_MS = 1000;

const makeStatus = (lock: FpStatus['lock'], telegram: FpStatus['telegram']): FpStatus => ({
  sensor: { online: true, capacity: 200, used: 3 },
  users_rev: 1,
  enroll: { state: 'idle', user_id: null, step: 0, steps: 5, slot: null, error: null },
  last_event: null,
  image: null,
  lock,
  telegram,
});

const TG_OFF: FpStatus['telegram'] = { enabled: false, online: false, error: null };
const TG_ONLINE: FpStatus['telegram'] = { enabled: true, online: true, error: null };
const TG_OFFLINE: FpStatus['telegram'] = { enabled: true, online: false, error: 'HTTP 401' };

// null: GET /fp/status never answers.
let status: FpStatus | null;
let lockRequests: unknown[];

const i18n = createI18n({ legacy: false, locale: 'en', messages, missingWarn: false, fallbackWarn: false });

const mountLock = async () => {
  const { default: Lock } = await import('@/views/Lock.vue');
  const router = createRouter({
    history: createMemoryHistory(),
    routes: [
      { path: '/', component: { template: '<div/>' } },
      { path: '/settings', component: { template: '<div/>' } },
    ],
  });
  await router.push('/');
  await router.isReady();
  const wrapper = mount(Lock, {
    global: {
      plugins: [i18n, router],
      stubs: { Layout: { template: '<div><slot /></div>' }, Heading: true },
    },
  });
  await flushPromises();
  return wrapper;
};

type LockWrapper = Awaited<ReturnType<typeof mountLock>>;

const RELAY_CARD = 0;
const TELEGRAM_CARD = 1;

// The pills in the header of a card, as "<class> <text>".
const pills = (wrapper: LockWrapper, card: number) =>
  wrapper.findAll('section.card')[card].findAll('.card-header .pill').map((pill) => {
    const tone = pill.classes().find((name) => (name === 'ok') || (name === 'danger')) ?? '?';
    return `${tone} ${pill.text()}`;
  });

const relaySwitch = (wrapper: LockWrapper) => wrapper.find('#lock_on');

const nextPoll = async () => {
  await vi.advanceTimersByTimeAsync(STATUS_POLL_MS);
  await flushPromises();
};

beforeEach(() => {
  hasSession.value = true;
  settingsRef.value = {
    lock_mode: 'strike', lock_addr: 1, lock_channel: 1, lock_pulse_ms: 3000,
    tg_api_url: 'https://api.telegram.org', tg_token: '', tg_admins: '',
  } as Partial<Settings>;
  status = makeStatus({ on: false, online: true }, TG_OFF);
  lockRequests = [];
  vi.mocked(api).mockReset().mockImplementation(async (path: string, options?: { json?: unknown }) => {
    if (path === 'fp/status') {
      if (status === null) {
        return new Promise(() => {});
      }
      // A new object on every answer, as from JSON on the wire.
      return structuredClone(status);
    }
    if (path === 'fp/tg_chats') {
      return { chats: [] };
    }
    if (path === 'fp/lock') {
      lockRequests.push(options?.json);
      return { success: true };
    }
    throw new Error(`unexpected request ${path}`);
  });
});

afterEach(() => {
  vi.useRealTimers();
  hasSession.value = false;
});

describe('Lock.vue — status pills', () => {
  it('LOCK-I-001: no pill on either card before the first status answer', async () => {
    status = null;
    const wrapper = await mountLock();

    expect(pills(wrapper, RELAY_CARD)).toEqual([]);
    expect(pills(wrapper, TELEGRAM_CARD)).toEqual([]);

    wrapper.unmount();
  });

  it('LOCK-I-002: the relay pill says whether the module answers', async () => {
    status = makeStatus({ on: false, online: true }, TG_OFF);
    let wrapper = await mountLock();
    expect(pills(wrapper, RELAY_CARD)).toEqual(['ok Online']);
    wrapper.unmount();

    status = makeStatus({ on: false, online: false }, TG_OFF);
    wrapper = await mountLock();
    expect(pills(wrapper, RELAY_CARD)).toEqual(['danger Offline']);
    wrapper.unmount();
  });

  it('LOCK-I-003: the Telegram pill appears only once a token is set', async () => {
    const cases: [FpStatus['telegram'], string[]][] = [
      [TG_OFF, []],
      [TG_ONLINE, ['ok Online']],
      [TG_OFFLINE, ['danger Offline']],
    ];
    for (const [telegram, expected] of cases) {
      status = makeStatus({ on: false, online: true }, telegram);
      const wrapper = await mountLock();
      expect(pills(wrapper, TELEGRAM_CARD), JSON.stringify(telegram)).toEqual(expected);
      wrapper.unmount();
    }
  });

  it('LOCK-I-003: the pills are in Russian too', async () => {
    i18n.global.locale.value = 'ru';
    status = makeStatus({ on: false, online: false }, TG_ONLINE);
    const wrapper = await mountLock();

    expect(pills(wrapper, RELAY_CARD)).toEqual(['danger Нет связи']);
    expect(pills(wrapper, TELEGRAM_CARD)).toEqual(['ok На связи']);

    wrapper.unmount();
    i18n.global.locale.value = 'en';
  });

  it('LOCK-I-004: the pills follow the status', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    status = makeStatus({ on: false, online: true }, TG_ONLINE);
    const wrapper = await mountLock();
    expect(pills(wrapper, RELAY_CARD)).toEqual(['ok Online']);
    expect(pills(wrapper, TELEGRAM_CARD)).toEqual(['ok Online']);

    status = makeStatus({ on: false, online: false }, TG_OFFLINE);
    await nextPoll();
    expect(pills(wrapper, RELAY_CARD)).toEqual(['danger Offline']);
    expect(pills(wrapper, TELEGRAM_CARD)).toEqual(['danger Offline']);

    // The token removed: the bot is off, which is not the same as offline.
    status = makeStatus({ on: false, online: true }, TG_OFF);
    await nextPoll();
    expect(pills(wrapper, RELAY_CARD)).toEqual(['ok Online']);
    expect(pills(wrapper, TELEGRAM_CARD)).toEqual([]);

    wrapper.unmount();
  });
});

describe('Lock.vue — why the bot is offline', () => {
  // Commit 58e9199c1 says the Lock page shows the bot offline "with that reason", and openapi.yaml
  // documents telegram.error for it; Lock.vue renders only the Offline pill. Un-skip once it does.
  it.skip('LOCK-I-006 BUG: the Telegram card names the reason the bot is offline', async () => {
    for (const error of ['HTTP 401', 'invalid token']) {
      status = makeStatus({ on: false, online: true }, { enabled: true, online: false, error });
      const wrapper = await mountLock();
      expect(wrapper.findAll('section.card')[TELEGRAM_CARD].text(), error).toContain(error);
      wrapper.unmount();
    }
  });
});

describe('Lock.vue — relay switch', () => {
  it('LOCK-I-005: shows lock.on, posts the state asked for, and falls back to what the relay reports', async () => {
    vi.useFakeTimers({ toFake: ['setTimeout', 'clearTimeout'] });
    status = makeStatus({ on: true, online: true }, TG_OFF);
    const wrapper = await mountLock();
    expect((relaySwitch(wrapper).element as HTMLInputElement).checked).toBe(true);

    await relaySwitch(wrapper).setValue(false);
    await flushPromises();
    expect(lockRequests).toEqual([{ on: false }]);

    // The relay did not follow: the next status answer puts the switch back.
    await nextPoll();
    expect((relaySwitch(wrapper).element as HTMLInputElement).checked).toBe(true);

    wrapper.unmount();
  });
});
