/**
 * Integration tests for TcpGateway.vue — the per-port TCP gateway enable switch.
 *
 * TG-I-001 — switch reflects state: port_mode 'tcp_bridge' → checked;
 *             'disabled' and 'passive' → unchecked.
 * TG-I-002 — toggling a 'disabled' port ON posts ports/N/mode { mode: 'tcp_bridge' }
 *             then refreshes via fetchInfo('low').
 * TG-I-003 — toggling a 'tcp_bridge' port OFF posts ports/N/mode { mode: 'disabled' }
 *             when the cache overlay is off.
 * TG-I-004 — toggling a 'tcp_bridge' port OFF posts ports/N/mode { mode: 'passive' }
 *             when the cache overlay is on (keep serial alive for the cache listener).
 * TG-I-005 — port isolation: toggling port 1 never touches ports/2 and vice-versa.
 * TG-I-006 — error path: a rejected api call reverts the optimistic switch state and
 *             surfaces showAlert('connection_error').
 * TG-I-007 — concurrency guard: a hanging api call makes a double-toggle post ports/N/mode
 *             exactly once.
 * TG-I-008 — disabled when info undefined: the enable switch is disabled and toggling it
 *             makes no ports/N/mode api call until info has loaded.
 *
 * Rendering note: the port cards are gated by `v-if="data"`, so the mocked useSettings()
 * must return a realistic Settings object. flushPromises() after mount lets the component
 * settle before inspecting the DOM.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import type { Info, PortMode, Settings } from '@/common/types';
import { api } from '@/utils/api';

// Shared reactive info ref — mutated per test scenario to simulate polls.
const infoRef = ref<Info | undefined>(undefined);

// Mocks — hoisted before any component import by Vitest's vi.mock hoisting.

const fetchInfoMock = vi.fn().mockResolvedValue(undefined);

// showAlert spy — asserted on the error path (revert + alert).
const showAlertMock = vi.fn();

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: fetchInfoMock }),
}));

vi.mock('@/common/alert', () => ({
  useAlerts: () => ({ showAlert: showAlertMock }),
}));

vi.mock('@/utils/api', () => ({
  // Provide a minimal resolved value so toggle requests do not throw by default.
  api: vi.fn().mockResolvedValue(undefined),
}));

vi.mock('@/common/hostname', () => ({
  useHostname: () => ({ hostname: ref('') }),
  setHostname: vi.fn(),
}));

// Build a realistic Settings object so the `v-if="data"` template branch renders.
function makeSettings(): Settings {
  const rs = () => ({
    term: false,
    fail_safe: false,
    tx_disabled: false,
    baudrate: 9600 as const,
    stopbits: '1' as const,
    parity: 'none' as const,
    databits: '8' as const,
    bridge: { mode: 'server' as const, ip: '0.0.0.0', port: 502, modbus: true },
  });
  return {
    hostname: 'test',
    login: 'admin',
    web_port: 80,
    io_bus: false,
    vout: false,
    cache_modbus_port: 504,
    cache_modbus_server_enabled: true,
    cache_value_timeout_s: 60,
    ethernet: { ip_static: '', mask_static: '', gw_static: '', dhcpc: true },
    rs485_1: { ...rs(), bridge: { ...rs().bridge, port: 502 } },
    rs485_2: { ...rs(), bridge: { ...rs().bridge, port: 503 } },
  };
}

const settingsData = ref<Settings | undefined>(makeSettings());

vi.mock('@/common/settings', () => ({
  useSettings: () => ({
    data: settingsData,
    initData: ref(null),
    isChanged: () => false,
    isLoading: ref(false),
    updateSettings: vi.fn(),
  }),
}));

// Stub @unhead/vue so Heading.vue's injectHead() / useHead() do not throw.
vi.mock('@unhead/vue', () => ({
  injectHead: () => ({}),
  useHead: vi.fn(),
  createUnhead: vi.fn(() => ({})),
  headSymbol: Symbol('head'),
}));

/** Build a minimal Info object with explicit per-port transport mode + cache overlay. */
function makeInfo(opts: {
  port1Mode: PortMode;
  port2Mode: PortMode;
  port1Cache?: boolean;
  port2Cache?: boolean;
}): Info {
  return {
    device_name: 'test',
    serial_num: 0,
    firmware: '0.0.0',
    hardware: '0',
    system_voltage: 0,
    heap_total: 0,
    heap_free: 0,
    heap_min_free: 0,
    ethernet: { con_eth: false, ip: '', mask: '', gw: '', mac: '' },
    wifi: {
      mode: 'none',
      con_ap: 0,
      con_sta: false,
      con_sta_ssid: '',
      enabled: false,
      sta_ip: '',
      sta_mask: '',
      sta_gw: '',
      sta_mac: '',
      ap_ip: '',
      ap_channel: 0,
      ap_mac: '',
    },
    rs485_1: {
      is_busy: false,
      error_percentage: 0,
      server_connections_count: 0,
      port_mode: opts.port1Mode,
      cache_enabled: opts.port1Cache ?? false,
    },
    rs485_2: {
      is_busy: false,
      error_percentage: 0,
      server_connections_count: 0,
      port_mode: opts.port2Mode,
      cache_enabled: opts.port2Cache ?? false,
    },
    cache_modbus_port: 504,
    cache_modbus_server_enabled: true,
    cache_value_timeout_s: 60,
    psram_available: false,
    psram_size_kb: 0,
  };
}

/** Return the checked state of the enable Switch for the given port key. */
function switchChecked(wrapper: ReturnType<typeof mount>, portKey: string): boolean {
  const el = wrapper.find(`#${portKey}-enabled`).element as HTMLInputElement;
  return el.checked;
}

/** Toggle the enable Switch for the given port key via a native checkbox change event. */
async function toggleSwitch(wrapper: ReturnType<typeof mount>, portKey: string): Promise<void> {
  const input = wrapper.find(`#${portKey}-enabled`);
  // The Switch emits update:modelValue on the checkbox 'change' event.
  await input.setValue(!(input.element as HTMLInputElement).checked);
  await flushPromises();
}

/**
 * Create a minimal stub vue-router instance.
 * Sidebar.vue (inside Layout.vue) calls useRoute()/useRouter(), so a router plugin is required.
 */
function makeRouter() {
  return createRouter({
    history: createMemoryHistory(),
    routes: [
      { path: '/', component: { template: '<div/>' } },
      { path: '/logout', component: { template: '<div/>' } },
    ],
  });
}

const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

beforeEach(() => {
  infoRef.value = undefined;
  settingsData.value = makeSettings();
  fetchInfoMock.mockClear();
  showAlertMock.mockClear();
  vi.resetModules();
  vi.mocked(api).mockReset();
  vi.mocked(api).mockResolvedValue(undefined as never);
});

describe('TG-I-001: enable switch reflects port_mode', () => {
  it('tcp_bridge → checked, disabled/passive → unchecked', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'tcp_bridge', port2Mode: 'disabled' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    expect(switchChecked(wrapper, 'rs485_1')).toBe(true);
    expect(switchChecked(wrapper, 'rs485_2')).toBe(false);

    // 'passive' must also read as unchecked (gateway is off, serial open for cache only).
    infoRef.value = makeInfo({ port1Mode: 'passive', port2Mode: 'tcp_bridge' });
    await flushPromises();

    expect(switchChecked(wrapper, 'rs485_1')).toBe(false);
    expect(switchChecked(wrapper, 'rs485_2')).toBe(true);

    wrapper.unmount();
  });
});

describe('TG-I-002: toggle ON', () => {
  it('disabled port → posts ports/1/mode { mode: tcp_bridge } then fetchInfo(low)', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();
    fetchInfoMock.mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });
});

describe('TG-I-003: toggle OFF (cache off → disabled)', () => {
  it('tcp_bridge port, cache_enabled=false → posts ports/1/mode { mode: disabled }', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'tcp_bridge', port2Mode: 'disabled', port1Cache: false });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'disabled' } });
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'passive' } });
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });
});

describe('TG-I-004: toggle OFF (cache on → passive)', () => {
  it('tcp_bridge port, cache_enabled=true → posts ports/1/mode { mode: passive }', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'tcp_bridge', port2Mode: 'disabled', port1Cache: true });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'passive' } });
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'disabled' } });
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });
});

describe('TG-I-005: port isolation', () => {
  it('toggling port 1 does not touch ports/2', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    wrapper.unmount();
  });

  it('toggling port 2 does not touch ports/1', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_2');

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/2/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());

    wrapper.unmount();
  });
});

describe('TG-I-006: error path', () => {
  it('rejected api call reverts the switch and calls showAlert(connection_error)', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // Port starts disabled → switch is unchecked.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Sanity: original state is unchecked.
    expect(switchChecked(wrapper, 'rs485_1')).toBe(false);

    // The toggle request rejects → component must revert the optimistic state and alert.
    vi.mocked(api).mockRejectedValueOnce(new Error('fail'));

    await toggleSwitch(wrapper, 'rs485_1');

    // showAlert was called with the connection error key.
    expect(showAlertMock).toHaveBeenCalledWith('connection_error');

    // The optimistic state reverted: the switch returns to its original (unchecked) state.
    expect(switchChecked(wrapper, 'rs485_1')).toBe(false);

    wrapper.unmount();
  });
});

describe('TG-I-007: concurrency guard', () => {
  it('double-toggle with a hanging request posts ports/1/mode exactly once', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // First toggle request hangs forever, keeping isToggling[rs485_1] = true.
    vi.mocked(api).mockImplementation(() => new Promise<never>(() => {}));
    vi.mocked(api).mockClear();

    const input = wrapper.find('#rs485_1-enabled');
    // Two SYNCHRONOUS change events fired back-to-back while the first request is in flight.
    // trigger('change') dispatches the DOM event synchronously, so the writable-computed
    // set fires on each one, invoking toggleEnabled twice without any await in between.
    // With guard (a) the 2nd toggleEnabled returns early → exactly ONE ports/1/mode POST.
    // Without guard (a) the 2nd call proceeds → TWO ports/1/mode POSTs.
    (input.element as HTMLInputElement).checked = true;
    input.trigger('change'); // no await — fire synchronously
    (input.element as HTMLInputElement).checked = false;
    input.trigger('change'); // no await — fire synchronously
    await flushPromises();

    const port1Calls = vi.mocked(api).mock.calls.filter((c: unknown[]) => c[0] === 'ports/1/mode');
    expect(port1Calls.length).toBe(1);

    wrapper.unmount();
  });
});

describe('TG-I-008: disabled when info undefined', () => {
  it('switch is disabled and toggling makes no ports/1/mode call until info loads', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // info never loads — the enable switch must be disabled and the toggle a no-op.
    infoRef.value = undefined;

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    const el = wrapper.find('#rs485_1-enabled').element as HTMLInputElement;
    expect(el.disabled).toBe(true);

    // Force the change handler to run even though the input is disabled. A raw
    // dispatchEvent fires registered listeners regardless of the :disabled attribute
    // (unlike a real user click, which the browser/jsdom suppresses), so the Switch's
    // change handler runs and assigns the writable computed → invokes its set →
    // calls toggleEnabled. This proves the internal `if (info.value === undefined) return`
    // guard — not the disabled attribute — is what prevents the ports/1/mode api call.
    el.checked = true;
    el.dispatchEvent(new Event('change'));
    await flushPromises();

    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());

    wrapper.unmount();
  });
});
