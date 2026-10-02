/**
 * Integration tests for the status pills, the relay switch and the two settings cards of Lock.vue.
 * The status poll (useFpStatus) is the real one; the settings are stubbed, and api answers GET
 * /fp/status, GET /fp/tg_chats and POST /fp/lock from the test.
 *
 * LOCK-I-001 — before the first status answer neither card has a pill.
 * LOCK-I-002 — the relay pill: Online (ok) while the module answers, Offline (danger) otherwise.
 * LOCK-I-003 — the Telegram pill appears only once a token is set: Online (ok), or Offline (danger)
 *               with the reason when the device gives one.
 * LOCK-I-004 — the pills follow the status as it changes.
 * LOCK-I-005 — the relay switch shows lock.on, posts the state asked for, and goes back to what
 *               the relay reports when the relay did not follow.
 * LOCK-I-006 — the Telegram card names the reason the bot is offline (telegram.error).
 * LOCK-I-007 — the relay card saves lock_exit_input with the other lock keys.
 * LOCK-I-008 — the Telegram card saves tg_notify_chat with the other bot keys.
 * LOCK-I-009 — the relay configuration pill: lock.config_error as a reason, none before the first
 *               try or once the module is configured, an unknown code shown as the device sent it.
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
const updateSettingsMock = vi.fn();

vi.mock('@/common/settings', () => ({
  SettingsRefreshError: class SettingsRefreshError extends Error {},
  useSettings: () => ({
    data: settingsRef,
    initData: settingsRef,
    isChanged: () => false,
    isLoading: ref(false),
    partialRefresh: vi.fn().mockResolvedValue(undefined),
    updateSettings: updateSettingsMock,
    refresh: vi.fn(),
  }),
}));

const STATUS_POLL_MS = 1000;

// config_error defaults to null: the relay module holds its configuration.
const makeStatus = (lock: Partial<FpStatus['lock']>, telegram: FpStatus['telegram']): FpStatus => ({
  sensor: { online: true, capacity: 200, used: 3 },
  users_rev: 1,
  enroll: { state: 'idle', user_id: null, step: 0, steps: 5, slot: null, error: null },
  last_event: null,
  image: null,
  lock: { on: false, online: true, config_error: null, ...lock },
  telegram,
});

const TG_OFF: FpStatus['telegram'] = { enabled: false, online: false, error: null };
const TG_ONLINE: FpStatus['telegram'] = { enabled: true, online: true, error: null };
const TG_OFFLINE: FpStatus['telegram'] = { enabled: true, online: false, error: 'HTTP 401' };
const TG_OFFLINE_NO_REASON: FpStatus['telegram'] = { enabled: true, online: false, error: null };

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

// The settings form of a card (the Telegram card also has the form that adds an allowed chat).
const settingsForm = (wrapper: LockWrapper, card: number) => wrapper.findAll('section.card')[card].find('form');

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
    lock_mode: 'strike', lock_addr: 1, lock_channel: 1, lock_pulse_ms: 3000, lock_exit_input: 'off',
    tg_api_url: 'https://api.telegram.org', tg_token: '', tg_admins: '', tg_notify_chat: '',
  } as Partial<Settings>;
  updateSettingsMock.mockReset().mockResolvedValue(undefined);
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
      [TG_OFFLINE, ['danger Offline: HTTP 401']],
      [TG_OFFLINE_NO_REASON, ['danger Offline']],
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
    expect(pills(wrapper, TELEGRAM_CARD)).toEqual(['danger Offline: HTTP 401']);

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
  // documents telegram.error for it.
  it('LOCK-I-006: the Telegram card names the reason the bot is offline', async () => {
    for (const error of ['HTTP 401', 'invalid token']) {
      status = makeStatus({ on: false, online: true }, { enabled: true, online: false, error });
      const wrapper = await mountLock();
      expect(wrapper.findAll('section.card')[TELEGRAM_CARD].text(), error).toContain(error);
      wrapper.unmount();
    }
  });
});

describe('Lock.vue — saving the cards', () => {
  it('LOCK-I-007: the relay card saves the exit button input with the other lock keys', async () => {
    const wrapper = await mountLock();
    await wrapper.find('#lock_exit_input').setValue('3');
    await settingsForm(wrapper, RELAY_CARD).trigger('submit');
    await flushPromises();

    expect(updateSettingsMock).toHaveBeenCalledTimes(1);
    expect(updateSettingsMock).toHaveBeenCalledWith({
      lock_mode: 'strike', lock_addr: 1, lock_channel: 1, lock_pulse_ms: 3000, lock_exit_input: '3',
    });

    wrapper.unmount();
  });

  it('LOCK-I-008: the Telegram card saves the notification chat with the other bot keys', async () => {
    const wrapper = await mountLock();
    await wrapper.find('#tg_notify_chat').setValue('-1001234567890');
    await settingsForm(wrapper, TELEGRAM_CARD).trigger('submit');
    await flushPromises();

    expect(updateSettingsMock).toHaveBeenCalledTimes(1);
    expect(updateSettingsMock).toHaveBeenCalledWith({
      tg_api_url: 'https://api.telegram.org', tg_token: '', tg_admins: '', tg_notify_chat: '-1001234567890',
    });

    wrapper.unmount();
  });
});

describe('Lock.vue — relay module configuration', () => {
  it('LOCK-I-009: the relay card names why the module is not configured', async () => {
    const cases: [string | null, string[]][] = [
      ['ESP_ERR_INVALID_RESPONSE', [
        'ok Online',
        'danger Not configured: the module answers with an error (relay firmware older than 1.28?)',
      ]],
      ['ESP_ERR_TIMEOUT', ['ok Online', 'danger Not configured: no answer from the module']],
      // No try since boot yet, and a module that holds its configuration: nothing to report.
      ['ESP_ERR_NOT_FINISHED', ['ok Online']],
      [null, ['ok Online']],
      ['ESP_ERR_INVALID_STATE', ['ok Online', 'danger Not configured: ESP_ERR_INVALID_STATE']],
    ];
    for (const [configError, expected] of cases) {
      status = makeStatus({ on: false, online: true, config_error: configError }, TG_OFF);
      const wrapper = await mountLock();
      expect(pills(wrapper, RELAY_CARD), String(configError)).toEqual(expected);
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
