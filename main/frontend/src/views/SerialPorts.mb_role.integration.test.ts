/**
 * Integration test for SerialPorts.vue — the build-time Modbus role.
 *
 * The role is chosen when the firmware is compiled (MB_ROLE=slave|master|none) and is reported
 * nowhere except by the shape of GET /settings: only a slave build carries `mb_slave_id` and
 * `mb_tcp_port`. The fixture below is the exact top-level key set a running master build returns.
 *
 * SP-I-001 — with both keys absent the Modbus card is gone (and with it its required/min/max
 *            inputs), while the RS-485 and I/O Bus cards still render and still save — each
 *            posting its own key only, never resurrecting the Modbus keys.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref, shallowRef } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory, matchedRouteKey } from 'vue-router';
import { messages } from '@/i18n/messages';
import type { Info, Settings } from '@/common/types';

const makeRs = () => ({
  baudrate: 9600 as const,
  parity: 'none' as const,
  stopbits: '1' as const,
  databits: '8' as const,
  term: false,
  fail_safe: false,
  tx_disabled: false,
});

// A master build's /settings: hostname, io_bus, login, pass, update_channel, vout, web_port,
// wifi_perm_disable (plus the rs485_* / ethernet sub-objects). No mb_slave_id, no mb_tcp_port.
const makeMasterSettings = (): Partial<Settings> => ({
  hostname: 'wb-mge',
  io_bus: true,
  login: 'admin',
  update_channel: 'stable',
  vout: false,
  web_port: 80,
  wifi_perm_disable: false,
  ethernet: { dhcpc: true, ip_static: '192.168.0.7', mask_static: '255.255.255.0', gw_static: '192.168.0.1' },
  rs485_1: makeRs(),
  rs485_2: makeRs(),
});

const makeInfo = (): Partial<Info> => ({ firmware: '1.3.2', signature: 'mge_v3', serial_num: 1, system_voltage: 12 });

const settingsRef = ref<Partial<Settings>>(makeMasterSettings());
const infoRef = ref<Partial<Info>>(makeInfo());
const updateSettingsMock = vi.fn().mockResolvedValue(undefined);

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: vi.fn().mockResolvedValue(undefined), startPolling: vi.fn(), stopPolling: vi.fn() }),
}));

vi.mock('@/common/settings', () => ({
  SettingsRefreshError: class SettingsRefreshError extends Error {},
  useSettings: () => ({
    data: settingsRef,
    initData: settingsRef,
    // Every card's Save is enabled, so a disabled button can never be the reason a save is missing.
    isChanged: () => true,
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
    { path: '/settings', component: { template: '<div/>' } },
  ],
});

const mountSerialPorts = async () => {
  const { default: SerialPorts } = await import('@/views/SerialPorts.vue');
  const router = makeRouter();
  await router.push('/settings');
  await router.isReady();
  const wrapper = mount(SerialPorts, {
    global: {
      plugins: [i18n, router],
      provide: { [matchedRouteKey as symbol]: shallowRef({ leaveGuards: new Set(), updateGuards: new Set() }) },
    },
  });
  await flushPromises();
  return wrapper;
};

beforeEach(() => {
  settingsRef.value = makeMasterSettings();
  updateSettingsMock.mockClear();
});

describe('SP-I-001: settings document without mb_slave_id / mb_tcp_port', () => {
  it('hides the Modbus card and still renders and saves the port cards', async () => {
    const wrapper = await mountSerialPorts();

    // The card and its two inputs are absent from the DOM...
    expect(wrapper.find('#mb_slave_id').exists()).toBe(false);
    expect(wrapper.find('#mb_tcp_port').exists()).toBe(false);
    // ...so no unfillable required/min/max constraint is left anywhere on the page, and every
    // form on it passes HTML constraint validation.
    expect(wrapper.findAll('[required]')).toHaveLength(0);
    const forms = wrapper.findAll('form');
    expect(forms.length).toBeGreaterThan(0);
    forms.forEach((form) => {
      expect((form.element as HTMLFormElement).checkValidity()).toBe(true);
    });

    // The port cards and the I/O Bus card are all there and editable.
    expect(wrapper.find('#rs485_1-baudrate').exists()).toBe(true);
    expect(wrapper.find('#rs485_2-baudrate').exists()).toBe(true);
    expect(wrapper.find('#io_bus').exists()).toBe(true);
    expect(wrapper.findAll('button[disabled]')).toHaveLength(0);

    // Saving each of them posts that card's own key and nothing else — in particular, no
    // mb_slave_id/mb_tcp_port sneaks back into a document that does not have them.
    for (const form of forms) {
      await form.trigger('submit');
    }
    await flushPromises();

    const payloads = updateSettingsMock.mock.calls.map(([payload]) => payload);
    expect(payloads.map((payload) => Object.keys(payload))).toEqual([['rs485_1'], ['rs485_2'], ['io_bus']]);
    payloads.forEach((payload) => {
      expect('mb_slave_id' in payload).toBe(false);
      expect('mb_tcp_port' in payload).toBe(false);
    });

    wrapper.unmount();
  });
});

