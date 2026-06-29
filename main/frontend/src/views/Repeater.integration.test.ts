/**
 * Integration tests for Repeater.vue — the single transparent RS-485 repeater toggle.
 *
 * REP-I-001 — toggle reflects state: both ports 'repeater' → ON (ENABLED);
 *              any other combination → OFF (DISABLED).
 * REP-I-002 — toggling ON (from off) posts ports/1/mode AND ports/2/mode with { mode: 'repeater' }.
 * REP-I-003 — toggling OFF posts both ports { mode: 'disabled' }.
 * REP-I-004 — stats from info.repeater (bytes_1to2/bytes_2to1/dropped_1/dropped_2) are rendered.
 * REP-I-005 — info without a `repeater` object (older firmware) renders without throwing,
 *              and the stats fall back to their zero placeholders.
 *
 * Rendering note: the page renders unconditionally; flushPromises() after mount lets the
 * component settle before inspecting the DOM.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import type { Info, PortMode, RepeaterStats, Settings } from '@/common/types';
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

// Build a realistic Settings object for the line-params display.
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
    initData: settingsData,
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

/**
 * Build a minimal Info object with explicit per-port mode + repeater stats.
 * Set `omitRepeater` to simulate older firmware whose GET /info has no `repeater` object.
 */
function makeInfo(opts: {
  port1Mode: PortMode;
  port2Mode: PortMode;
  repeater?: Partial<RepeaterStats>;
  omitRepeater?: boolean;
}): Info {
  const repeater: RepeaterStats | undefined = opts.omitRepeater
    ? undefined
    : {
      active: opts.port1Mode === 'repeater' && opts.port2Mode === 'repeater',
      uptime_s: 0,
      bytes_1to2: 0,
      bytes_2to1: 0,
      dropped_1: 0,
      dropped_2: 0,
      ...opts.repeater,
    };
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
      cache_enabled: false,
    },
    rs485_2: {
      is_busy: false,
      error_percentage: 0,
      server_connections_count: 0,
      port_mode: opts.port2Mode,
      cache_enabled: false,
    },
    repeater,
    cache_modbus_port: 504,
    cache_modbus_server_enabled: true,
    cache_value_timeout_s: 60,
    psram_available: false,
    psram_size_kb: 0,
  };
}

/** The repeater toggle button. */
function toggleButton(wrapper: ReturnType<typeof mount>) {
  return wrapper.find('.rep-toggle');
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

describe('REP-I-001: toggle reflects repeater state', () => {
  it('both ports repeater → ON; any other combo → OFF', async () => {
    const { default: Repeater } = await import('@/views/Repeater.vue');

    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater' });

    const wrapper = mount(Repeater, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    expect(toggleButton(wrapper).classes()).toContain('on');
    expect(toggleButton(wrapper).attributes('aria-pressed')).toBe('true');

    // Only one port in repeater → OFF.
    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'disabled' });
    await flushPromises();

    expect(toggleButton(wrapper).classes()).toContain('off');
    expect(toggleButton(wrapper).attributes('aria-pressed')).toBe('false');

    wrapper.unmount();
  });
});

describe('REP-I-002: toggle ON', () => {
  it('posts ports/1/mode AND ports/2/mode { mode: repeater } then fetchInfo(low)', async () => {
    const { default: Repeater } = await import('@/views/Repeater.vue');

    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled' });

    const wrapper = mount(Repeater, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();
    fetchInfoMock.mockClear();

    await toggleButton(wrapper).trigger('click');
    await flushPromises();

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'repeater' } });
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/2/mode', { method: 'POST', json: { mode: 'repeater' } });
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });
});

describe('REP-I-003: toggle OFF', () => {
  it('posts ports/1/mode AND ports/2/mode { mode: disabled }', async () => {
    const { default: Repeater } = await import('@/views/Repeater.vue');

    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater' });

    const wrapper = mount(Repeater, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleButton(wrapper).trigger('click');
    await flushPromises();

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'disabled' } });
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/2/mode', { method: 'POST', json: { mode: 'disabled' } });
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'repeater' } });

    wrapper.unmount();
  });
});

describe('REP-I-004: stats render', () => {
  it('forward/reverse bytes and per-port dropped bytes appear in the DOM', async () => {
    const { default: Repeater } = await import('@/views/Repeater.vue');

    infoRef.value = makeInfo({
      port1Mode: 'repeater',
      port2Mode: 'repeater',
      repeater: {
        uptime_s: 5047,
        bytes_1to2: 18472,
        bytes_2to1: 12345,
        dropped_1: 3,
        dropped_2: 7,
      },
    });

    const wrapper = mount(Repeater, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Normalize any kind of whitespace (the space between value and unit, and the U+00A0
    // no-break thousands separator) to a single regular space so the assertions are robust.
    const text = wrapper.text().replace(/\s+/g, ' ');
    // Forward and reverse byte counts are auto-scaled to a binary (1024) unit:
    // 18472 B -> 18.0 KB, 12345 B -> 12.1 KB.
    expect(text).toContain('18.0 KB');
    expect(text).toContain('12.1 KB');
    // Per-port dropped bytes (the dt label and dd value render as adjacent elements, so the
    // collapsed text concatenates them with no separating space: dropped_1=3, dropped_2=7).
    expect(text).toContain('Dropped bytes3');
    expect(text).toContain('Dropped bytes7');
    // Uptime formatted HH:MM:SS (5047s = 01:24:07).
    expect(text).toContain('01:24:07');

    wrapper.unmount();
  });
});

describe('REP-I-005: missing repeater object', () => {
  it('renders without throwing and falls back to zero placeholders', async () => {
    const { default: Repeater } = await import('@/views/Repeater.vue');

    // Older firmware: GET /info has no `repeater` field; ports not in repeater mode.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', omitRepeater: true });

    let wrapper: ReturnType<typeof mount> | undefined;
    expect(() => {
      wrapper = mount(Repeater, { global: { plugins: [i18n, makeRouter()] } });
    }).not.toThrow();
    await flushPromises();

    const text = wrapper!.text().replace(/\s+/g, ' ');
    // The per-port dropped-byte stats fall back to 0 (dt label + dd value render adjacent).
    expect(text).toContain('Dropped bytes0');
    // The toggle reflects the off state.
    expect(toggleButton(wrapper!).classes()).toContain('off');

    wrapper!.unmount();
  });
});

describe('REP-I-006: toggle error path', () => {
  it('a rejected api() reverts the toggle and raises the connection alert', async () => {
    const { default: Repeater } = await import('@/views/Repeater.vue');

    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled' });
    const wrapper = mount(Repeater, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // The toggle attempt fails at the API layer.
    vi.mocked(api).mockRejectedValue(new Error('connection refused'));

    await toggleButton(wrapper).trigger('click');
    await flushPromises();

    // onError wiring fired with the connection_error key...
    expect(showAlertMock).toHaveBeenCalledWith('connection_error');
    // ...and the optimistic ON state reverted back to OFF.
    expect(toggleButton(wrapper).classes()).toContain('off');
    expect(toggleButton(wrapper).attributes('aria-pressed')).toBe('false');

    wrapper.unmount();
  });
});
