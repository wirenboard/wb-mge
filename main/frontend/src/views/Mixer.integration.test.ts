/**
 * Integration tests for Mixer.vue — the live state, the program editor and what a save posts.
 *
 * MIX-I-001 — the live cards render the mixer object of /info: state, the running step with its
 *              description, the countdown, speed, direction and the drive's own readings.
 * MIX-I-002 — editing a program: add a step, retype it, reorder it, rename the program; the save
 *              posts that program's two keys only, and they carry the edited steps.
 * MIX-I-003 — the editor's Save is dead until something actually changes, and a removed step
 *              wakes it up.
 * MIX-I-004 — the control and connection cards each post their own mix_* keys and nothing else:
 *              the whole settings document does not fit the firmware's 4 KB request body.
 * MIX-I-005 — /info is polled once a second while the page is open and not once after it is left.
 * MIX-I-006 — the editor shows what a typed speed becomes after the conversion and the drive's
 *              own limits, and refuses a zero speed on anything but a pause.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref, shallowRef } from 'vue';
import type { VueWrapper } from '@vue/test-utils';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory, matchedRouteKey } from 'vue-router';
import {
  MIXER_STEP_FORWARD,
  MIXER_STEP_PAUSE,
  MIXER_STEP_REVERSE,
  decodeProgram,
  encodeProgram,
} from '@/common/mixerProgram';
import { messages } from '@/i18n/messages';
import type { Info, MixerInfo, Settings } from '@/common/types';

const PROGRAM_0 = encodeProgram([
  { type: MIXER_STEP_FORWARD, speed: 300, duration: 60 },
  { type: MIXER_STEP_REVERSE, speed: 400, duration: 120 },
  { type: MIXER_STEP_PAUSE, speed: 0, duration: 30 },
]);
const PROGRAM_1 = encodeProgram([{ type: MIXER_STEP_FORWARD, speed: 200, duration: 90 }]);

const makeRs = () => ({
  baudrate: 9600 as const,
  parity: 'none' as const,
  stopbits: '1' as const,
  databits: '8' as const,
  term: false,
  fail_safe: false,
  tx_disabled: false,
});

const makeSettings = (): Partial<Settings> => ({
  hostname: 'wb-mge',
  login: 'admin',
  web_port: 80,
  io_bus: true,
  vout: false,
  update_channel: 'stable',
  rs485_1: makeRs(),
  rs485_2: makeRs(),
  mix_enabled: true,
  mix_prog: 0,
  mix_p0: PROGRAM_0,
  mix_p1: PROGRAM_1,
  mix_p2: '',
  mix_p3: '',
  mix_p0_name: 'Slip',
  mix_p1_name: 'Rinse',
  mix_p2_name: '',
  mix_p3_name: '',
  mix_rpm_50hz: 1400,
  mix_vfd_port: 2,
  mix_vfd_addr: 12,
  mix_rev_code: 3,
});

const makeMixer = (): MixerInfo => ({
  state: 'running',
  program: 0,
  step: 1,
  step_left: 95,
  speed: 400,
  step_type: 2,
  vfd_online: true,
  frequency: 42.5,
  voltage: 311,
  current: 3.25,
  temperature: 47,
  cmd_raw: 5,
  write_err: 'ESP_ERR_INVALID_RESPONSE',
});

const makeInfo = (): Partial<Info> => ({
  firmware: '1.3.2',
  signature: 'mge_v3',
  serial_num: 1,
  system_voltage: 12,
  mixer: makeMixer(),
});

const settingsRef = ref<Partial<Settings>>(makeSettings());
const infoRef = ref<Partial<Info>>(makeInfo());
const updateSettingsMock = vi.fn().mockResolvedValue(undefined);
const fetchInfoMock = vi.fn().mockResolvedValue(undefined);
const isChangedMock = vi.fn(() => true);

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: fetchInfoMock, startPolling: vi.fn(), stopPolling: vi.fn() }),
}));

vi.mock('@/common/settings', () => ({
  SettingsRefreshError: class SettingsRefreshError extends Error {},
  useSettings: () => ({
    data: settingsRef,
    initData: settingsRef,
    isChanged: isChangedMock,
    isLoading: ref(false),
    partialRefresh: vi.fn().mockResolvedValue(undefined),
    updateSettings: updateSettingsMock,
    refresh: vi.fn(),
  }),
}));

vi.mock('@/utils/api', () => ({
  api: vi.fn().mockResolvedValue({}),
}));

vi.mock('@unhead/vue', () => ({
  injectHead: () => ({}),
  useHead: vi.fn(),
  createUnhead: vi.fn(() => ({})),
  headSymbol: Symbol('head'),
}));

const i18n = createI18n({ legacy: false, locale: 'en', messages, missingWarn: false, fallbackWarn: false });

const makeRouter = () => createRouter({
  history: createMemoryHistory(),
  routes: [
    { path: '/', component: { template: '<div/>' } },
    { path: '/logout', component: { template: '<div/>' } },
    { path: '/mixer', component: { template: '<div/>' } },
  ],
});

const mountMixer = async () => {
  const { default: Mixer } = await import('@/views/Mixer.vue');
  const router = makeRouter();
  await router.push('/mixer');
  await router.isReady();
  const wrapper = mount(Mixer, {
    global: {
      plugins: [i18n, router],
      provide: { [matchedRouteKey as symbol]: shallowRef({ leaveGuards: new Set(), updateGuards: new Set() }) },
    },
  });
  await flushPromises();
  return wrapper;
};

// The page carries three forms; each test names the one it submits by a control only that form has.
const formWith = (wrapper: VueWrapper, selector: string) =>
  wrapper.findAll('form').find((form) => form.find(selector).exists())!;

const textOf = (wrapper: VueWrapper, selector: string) =>
  wrapper.find(selector).text().replace(/\s+/g, ' ').trim();

const payloads = () => updateSettingsMock.mock.calls.map(([payload]) => payload as Record<string, unknown>);

beforeEach(() => {
  settingsRef.value = makeSettings();
  infoRef.value = makeInfo();
  updateSettingsMock.mockClear();
  fetchInfoMock.mockClear();
  isChangedMock.mockClear();
});

afterEach(() => {
  vi.useRealTimers();
});

describe('MIX-I-001: live state', () => {
  it('renders the mixer and drive readings of /info', async () => {
    const wrapper = await mountMixer();

    expect(wrapper.text()).toContain('Running');
    // step 1 is the second entry of the running program, which is the reverse one.
    expect(textOf(wrapper, '#mixer_step')).toBe('Step 2 of 3 · Reverse · 400 rpm · 2 min 0 s');
    expect(textOf(wrapper, '#mixer_step_left')).toBe('1 min 35 s');
    expect(textOf(wrapper, '#mixer_speed')).toBe('400 rpm');
    expect(textOf(wrapper, '#mixer_direction')).toBe('Reverse');

    expect(textOf(wrapper, '#mixer_frequency')).toBe('42.5 Hz');
    expect(textOf(wrapper, '#mixer_current')).toBe('3.25 A');
    expect(textOf(wrapper, '#mixer_voltage')).toBe('311 V');
    expect(textOf(wrapper, '#mixer_temperature')).toBe('47 °C');
    expect(wrapper.text()).toContain('Link');

    wrapper.unmount();
  });

  it('says the firmware has no mixer instead of showing cards nothing feeds', async () => {
    infoRef.value = { ...makeInfo(), mixer: undefined };
    const wrapper = await mountMixer();

    expect(wrapper.find('#mixer_absent').exists()).toBe(true);
    expect(wrapper.text()).toContain('This firmware has no mixer');

    // A build without the engine reads none of the mix_* settings either, so neither the live
    // cards nor the editor are rendered at all — dashes and a Save button that changes nothing
    // are worse than saying why there is nothing here.
    expect(wrapper.find('#mixer_frequency').exists()).toBe(false);
    expect(wrapper.find('#mix_prog').exists()).toBe(false);
    expect(wrapper.find('#mix_rpm_50hz').exists()).toBe(false);
    expect(wrapper.find('#mix_save_program').exists()).toBe(false);

    wrapper.unmount();
  });
});

describe('MIX-I-002: editing a program', () => {
  it('saves the edited steps under that program keys only', async () => {
    const wrapper = await mountMixer();

    await wrapper.find('#mix_edited').setValue('1');
    expect(wrapper.find('#mix_step_0_speed').element).toHaveProperty('value', '200');

    await wrapper.find('#mix_step_add').trigger('click');
    await wrapper.find('#mix_step_1_type').setValue('2');
    await wrapper.find('#mix_step_1_speed').setValue('1500');
    await wrapper.find('#mix_step_1_min').setValue('2');
    await wrapper.find('#mix_step_1_sec').setValue('30');
    await wrapper.find('#mix_step_1_up').trigger('click');
    await wrapper.find('#mix_name').setValue('Wash');

    await formWith(wrapper, '#mix_edited').trigger('submit');
    await flushPromises();

    expect(updateSettingsMock).toHaveBeenCalledTimes(1);
    const payload = payloads()[0];
    expect(Object.keys(payload)).toEqual(['mix_p1', 'mix_p1_name']);
    expect(payload.mix_p1_name).toBe('Wash');
    expect(decodeProgram(payload.mix_p1 as string)).toEqual([
      { type: MIXER_STEP_REVERSE, speed: 1500, duration: 150 },
      { type: MIXER_STEP_FORWARD, speed: 200, duration: 90 },
    ]);

    wrapper.unmount();
  });

  it('keeps the eleven-step ceiling', async () => {
    const wrapper = await mountMixer();

    await wrapper.find('#mix_edited').setValue('1');
    for (let added = 1; added < 11; added += 1) {
      await wrapper.find('#mix_step_add').trigger('click');
    }

    expect(wrapper.findAll('.mixer-step')).toHaveLength(11);
    expect(wrapper.find('#mix_step_add').attributes('disabled')).toBeDefined();

    wrapper.unmount();
  });
});

describe('MIX-I-003: the editor Save button', () => {
  it('is disabled until the program actually changes', async () => {
    const wrapper = await mountMixer();

    expect(wrapper.find('#mix_save_program').attributes('disabled')).toBeDefined();

    await wrapper.find('#mix_step_0_remove').trigger('click');
    expect(wrapper.find('#mix_save_program').attributes('disabled')).toBeUndefined();

    wrapper.unmount();
  });
});

describe('MIX-I-004: the other two cards', () => {
  it('post their own mix_ keys and nothing else', async () => {
    const wrapper = await mountMixer();

    await formWith(wrapper, '#mix_enabled').trigger('submit');
    await formWith(wrapper, '#mix_vfd_addr').trigger('submit');
    await flushPromises();

    expect(payloads().map((payload) => Object.keys(payload))).toEqual([
      ['mix_enabled', 'mix_prog'],
      ['mix_rpm_50hz', 'mix_vfd_port', 'mix_vfd_addr', 'mix_rev_code'],
    ]);
    payloads().forEach((payload) => {
      expect(Object.keys(payload).every((key) => key.startsWith('mix_'))).toBe(true);
    });

    wrapper.unmount();
  });
});

describe('MIX-I-006: what a typed speed will really be', () => {
  it('shows the frequency and the shaft speed the device computes from it', async () => {
    const wrapper = await mountMixer();

    await wrapper.find('#mix_edited').setValue('1');

    // The step asks for 200 rpm, and the device gives 1400 rpm per 50 Hz: 200 * 500 / 1400
    // truncates to 7.1 Hz, and 7.1 Hz turns the shaft at 199 rpm rather than the 200 typed.
    expect(textOf(wrapper, '#mix_step_0_effect')).toBe('7.1 Hz · 199 rpm');

    // Below the drive's 1.0 Hz floor the speed is raised to it, and the shaft turns at three
    // times what was asked for. That is exactly what has to be visible before saving.
    await wrapper.find('#mix_step_0_speed').setValue('10');
    expect(textOf(wrapper, '#mix_step_0_effect')).toBe('1.0 Hz · 28 rpm');

    // And zero is not offered at all on a step that moves the shaft.
    expect(wrapper.find('#mix_step_0_speed').attributes('min')).toBe('10');

    wrapper.unmount();
  });

  it('offers zero only on a pause, which is what standing still is for', async () => {
    const wrapper = await mountMixer();

    await wrapper.find('#mix_edited').setValue('1');
    await wrapper.find('#mix_step_0_type').setValue('0');

    expect(wrapper.find('#mix_step_0_speed').attributes('min')).toBe('0');
    expect(wrapper.find('#mix_step_0_effect').exists()).toBe(false);

    await formWith(wrapper, '#mix_edited').trigger('submit');
    await flushPromises();
    expect(decodeProgram(payloads()[0].mix_p1 as string)).toEqual([
      { type: MIXER_STEP_PAUSE, speed: 0, duration: 90 },
    ]);

    wrapper.unmount();
  });
});

describe('MIX-I-005: the /info poll', () => {
  it('runs once a second while the page is open and stops when it is left', async () => {
    vi.useFakeTimers();
    const wrapper = await mountMixer();

    expect(fetchInfoMock).not.toHaveBeenCalled(); // the route guard has already fetched once
    await vi.advanceTimersByTimeAsync(3000);
    expect(fetchInfoMock).toHaveBeenCalledTimes(3);

    wrapper.unmount();
    await vi.advanceTimersByTimeAsync(5000);
    expect(fetchInfoMock).toHaveBeenCalledTimes(3);
  });
});
