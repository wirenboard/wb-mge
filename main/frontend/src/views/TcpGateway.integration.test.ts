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
 * TG-I-005 — port isolation: the peer is written only where enabling would strand it. On the
 *             ON path that means every peer mode OTHER than 'repeater' is left untouched; on
 *             the OFF path the peer is left untouched unconditionally, 'repeater' included.
 * TG-I-006 — error path: a rejected api call reverts the optimistic switch state and
 *             surfaces showAlert('connection_error').
 * TG-I-007 — concurrency guard: a hanging api call makes a double-toggle post ports/N/mode
 *             exactly once, and the in-flight toggle locks the OTHER port's switch too
 *             (the ON path writes both ports of the pair).
 * TG-I-008 — disabled when info undefined: the enable switch is disabled and toggling it
 *             makes no ports/N/mode api call until info has loaded.
 * TG-I-009 — repeater peer: enabling the gateway on one port takes the PEER out of 'repeater'
 *             first ('passive' when the peer's cache overlay is on, else 'disabled'), then
 *             opens this port as 'tcp_bridge'. A repeater is a pair, so a port left alone in
 *             'repeater' forwards nothing. Either request can fail: a failed peer request
 *             sends nothing else, a failed gateway request leaves THIS port in 'repeater' and
 *             the same click repairs it. Both surface the alert.
 * TG-I-010 — the repeater banner on the peer card clears once the peer leaves 'repeater',
 *             with no separate condition of its own.
 * TG-I-011 — fresh-state decision: the peer's fate is decided from a re-read of info, not from
 *             the cached copy, so clicking one port and then the other does not undo the
 *             gateway the first click brought up.
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
    update_channel: 'stable',
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

/** Text content of the port card that owns the given port's enable switch. */
function cardText(wrapper: ReturnType<typeof mount>, portKey: string): string {
  const el = wrapper.find(`#${portKey}-enabled`).element;
  return el.closest('section.card')?.textContent ?? '';
}

/** English text of the repeater banner (TcpGateway.vue i18n key 'repeater_active'). */
const REPEATER_BANNER = 'turns off the repeater currently active on this port';

/** The mode of every recorded ports/N/mode POST, in call order: ['ports/1/mode', 'passive']. */
function postedModes(): [string, PortMode][] {
  return vi.mocked(api).mock.calls
    .map((c) => [c[0] as string, (c[1] as { json?: { mode?: PortMode } } | undefined)?.json?.mode])
    .filter((c): c is [string, PortMode] => c[1] !== undefined);
}

/**
 * Replay the recorded ports/N/mode POSTs onto a starting mode pair, so a test can feed back
 * the info poll the device would report AFTER the component's requests — derived from what
 * the component actually posted rather than from what the test expects it to post.
 */
function applyPostedModes(start: { port1Mode: PortMode; port2Mode: PortMode }) {
  const modes = { ...start };
  for (const [url, mode] of postedModes()) {
    if (url === 'ports/1/mode') modes.port1Mode = mode;
    if (url === 'ports/2/mode') modes.port2Mode = mode;
  }
  return modes;
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
  // mockReset, not mockClear: TG-I-011 installs an implementation that publishes device state
  // into infoRef, and it must not leak into the tests that run after it.
  fetchInfoMock.mockReset();
  fetchInfoMock.mockResolvedValue(undefined);
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

describe('TG-I-005: port isolation (the peer is written only to clear a repeater on enable)', () => {
  // Every peer mode except 'repeater'. Isolation is deliberate for all of them: only a
  // repeater peer is a half of a pair that this port's mode change would strand (TG-I-009).
  const peerModes: PortMode[] = ['disabled', 'passive', 'tcp_bridge'];

  it.each(peerModes)('toggling port 1 does not touch ports/2 when port 2 is %s', async (port2Mode) => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode, port2Cache: true });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    wrapper.unmount();
  });

  it.each(peerModes)('toggling port 2 does not touch ports/1 when port 1 is %s', async (port1Mode) => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode, port2Mode: 'disabled', port1Cache: true });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_2');

    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/2/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());

    wrapper.unmount();
  });

  it('turning a gateway OFF never touches the peer, even a repeater peer', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // Port 1 is a live gateway, port 2 sits in 'repeater'. Switching port 1 off does not
    // enable anything, so there is no stranded half to clean up: only the OFF branch runs.
    infoRef.value = makeInfo({ port1Mode: 'tcp_bridge', port2Mode: 'repeater' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(postedModes()).toEqual([['ports/1/mode', 'disabled']]);

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

  it('a toggle in flight on one port disables the other port and blocks its toggle', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // Both ports in 'repeater': a toggle on either one writes BOTH ports, so the per-port
    // guard inside useOptimisticToggle is not enough — port 2 must be locked out while
    // port 1's two-request sequence is still running, or the two sequences interleave.
    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // The peer request hangs forever, so port 1's toggle stays in flight.
    vi.mocked(api).mockImplementation(() => new Promise<never>(() => {}));
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    // Only the peer request went out and it never resolved.
    expect(postedModes()).toEqual([['ports/2/mode', 'disabled']]);
    // Both switches are locked, not just port 1's.
    expect((wrapper.find('#rs485_1-enabled').element as HTMLInputElement).disabled).toBe(true);
    expect((wrapper.find('#rs485_2-enabled').element as HTMLInputElement).disabled).toBe(true);

    // A change event dispatched at the disabled switch still reaches the handler, which is
    // why the guard has to live in toggleEnabled() and not only in the :disabled binding.
    const el = wrapper.find('#rs485_2-enabled').element as HTMLInputElement;
    el.checked = true;
    el.dispatchEvent(new Event('change'));
    await flushPromises();

    // Nothing new was sent: no second sequence started under the first one.
    expect(postedModes()).toEqual([['ports/2/mode', 'disabled']]);

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

describe('TG-I-009: repeater peer is taken out of repeater', () => {
  it('both ports in repeater, peer cache off: port 2 goes to disabled BEFORE port 1 opens as tcp_bridge', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    // A repeater port is not a gateway, so the switch reads OFF and the flip takes the ON path.
    expect(switchChecked(wrapper, 'rs485_1')).toBe(false);

    await toggleSwitch(wrapper, 'rs485_1');

    // Order matters: the peer request goes first, so a failure of it leaves the pair untouched.
    expect(postedModes()).toEqual([
      ['ports/2/mode', 'disabled'],
      ['ports/1/mode', 'tcp_bridge'],
    ]);
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });

  it('peer cache overlay on: the peer goes to passive, not disabled', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // port2Cache keeps the Register Map cache listener alive, so the peer must stay open
    // as 'passive' — the same rule the OFF branch applies to the port being switched off.
    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater', port2Cache: true });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(postedModes()).toEqual([
      ['ports/2/mode', 'passive'],
      ['ports/1/mode', 'tcp_bridge'],
    ]);

    wrapper.unmount();
  });

  it('the peer target follows the PEER cache flag, not the flag of the port being enabled', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // Cache on for the port being enabled, off for the peer: the peer must still go to
    // 'disabled'. Reading the wrong port's flag would send 'passive' here.
    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater', port1Cache: true, port2Cache: false });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(postedModes()).toEqual([
      ['ports/2/mode', 'disabled'],
      ['ports/1/mode', 'tcp_bridge'],
    ]);

    wrapper.unmount();
  });

  it('symmetric: enabling port 2 takes port 1 out of repeater first', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater', port1Cache: true });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_2');

    expect(postedModes()).toEqual([
      ['ports/1/mode', 'passive'],
      ['ports/2/mode', 'tcp_bridge'],
    ]);

    wrapper.unmount();
  });

  it('a lone repeater peer is cleaned up too', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // Port 2 alone in 'repeater' is already a dead mode (it forwards nothing). Enabling the
    // gateway on port 1 clears it as well: the condition is the PEER's mode, not the pair's.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'repeater' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');

    expect(postedModes()).toEqual([
      ['ports/2/mode', 'disabled'],
      ['ports/1/mode', 'tcp_bridge'],
    ]);

    wrapper.unmount();
  });

  it('a failed peer request stops before ports/1/mode, reverts the switch and alerts', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    // The peer request (the first of the two) rejects.
    vi.mocked(api).mockRejectedValueOnce(new Error('fail'));

    await toggleSwitch(wrapper, 'rs485_1');

    // Nothing was changed on the device: the gateway request was never sent.
    expect(postedModes()).toEqual([['ports/2/mode', 'disabled']]);
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());
    // The optimistic-toggle error path still surfaces through the two-call action.
    expect(showAlertMock).toHaveBeenCalledWith('connection_error');
    expect(switchChecked(wrapper, 'rs485_1')).toBe(false);

    wrapper.unmount();
  });

  it('a failed gateway request after a successful peer request strands THIS port, and the same click repairs it', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'repeater' });

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();
    vi.mocked(api).mockClear();

    // The peer request succeeds; the gateway request — the SECOND of the two — rejects.
    // Sequencing them this way is the mirror of the case above, and it is the half-configured
    // state the request order confines rather than eliminates: port 1 stays in 'repeater'.
    vi.mocked(api)
      .mockResolvedValueOnce(undefined as never)
      .mockRejectedValueOnce(new Error('fail'));

    await toggleSwitch(wrapper, 'rs485_1');

    expect(postedModes()).toEqual([
      ['ports/2/mode', 'disabled'],
      ['ports/1/mode', 'tcp_bridge'],
    ]);
    expect(showAlertMock).toHaveBeenCalledWith('connection_error');
    // The optimistic override reverted: the gateway never came up.
    expect(switchChecked(wrapper, 'rs485_1')).toBe(false);

    // The poll that follows reports what the device actually holds now: the peer left
    // 'repeater', this port did not.
    infoRef.value = makeInfo({ port1Mode: 'repeater', port2Mode: 'disabled' });
    await flushPromises();

    // The banner names the port that is actually stranded — this one, not the peer.
    expect(cardText(wrapper, 'rs485_1')).toContain(REPEATER_BANNER);
    expect(cardText(wrapper, 'rs485_2')).not.toContain(REPEATER_BANNER);

    // Retry: the same click repairs it. The peer is no longer in 'repeater', so exactly one
    // request goes out — this port's gateway — instead of re-running the pair.
    vi.mocked(api).mockClear();
    await toggleSwitch(wrapper, 'rs485_1');

    expect(postedModes()).toEqual([['ports/1/mode', 'tcp_bridge']]);

    wrapper.unmount();
  });
});

describe('TG-I-010: the repeater banner clears with the peer mode', () => {
  it('shows on both cards while both ports are repeater, and on neither after the flip', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    const start = { port1Mode: 'repeater' as PortMode, port2Mode: 'repeater' as PortMode };
    infoRef.value = makeInfo(start);

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    expect(cardText(wrapper, 'rs485_1')).toContain(REPEATER_BANNER);
    expect(cardText(wrapper, 'rs485_2')).toContain(REPEATER_BANNER);

    vi.mocked(api).mockClear();
    await toggleSwitch(wrapper, 'rs485_1');

    // The poll that fetchInfo('low') triggers, built from the modes the component actually
    // posted — so the banner assertions below depend on the peer request having been sent.
    infoRef.value = makeInfo(applyPostedModes(start));
    await flushPromises();

    // The banner condition is just port_mode === 'repeater'; no separate fix is needed.
    expect(cardText(wrapper, 'rs485_1')).not.toContain(REPEATER_BANNER);
    expect(cardText(wrapper, 'rs485_2')).not.toContain(REPEATER_BANNER);

    wrapper.unmount();
  });
});

describe('TG-I-011: the peer decision is made from fresh state, not the cached info', () => {
  it('leaving the repeater by enabling both ports in turn: the second click keeps the first gateway', async () => {
    const { default: TcpGateway } = await import('@/views/TcpGateway.vue');

    // Both ports in 'repeater' → both switches read OFF, so the user clicks them one after
    // the other to make both ports gateways.
    const device = { port1Mode: 'repeater' as PortMode, port2Mode: 'repeater' as PortMode };
    infoRef.value = makeInfo(device);

    const wrapper = mount(TcpGateway, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Stand-in firmware: every accepted ports/N/mode POST moves the device state.
    vi.mocked(api).mockImplementation(async (url: string, opts?: unknown) => {
      const mode = (opts as { json?: { mode?: PortMode } } | undefined)?.json?.mode;
      if (mode !== undefined) {
        if (url === 'ports/1/mode') device.port1Mode = mode;
        if (url === 'ports/2/mode') device.port2Mode = mode;
      }
      return undefined as never;
    });

    // The refresh useOptimisticToggle fires after a toggle is low priority and fire-and-forget:
    // leave it pending so `info` still holds the pre-click modes when the second click lands.
    // That is the 5 s-stale window this test exists for. The component's own explicit re-read
    // (no priority argument) resolves and publishes the real device state.
    fetchInfoMock.mockImplementation((priority?: string) => {
      if (priority === 'low') return new Promise<void>(() => {}); // never settles
      infoRef.value = makeInfo(device);
      return Promise.resolve();
    });

    vi.mocked(api).mockClear();

    await toggleSwitch(wrapper, 'rs485_1');
    await toggleSwitch(wrapper, 'rs485_2');

    // The second click must NOT post 'disabled' to port 1: reading the peer's mode from the
    // stale cache (still 'repeater' for both ports) is exactly what made it do that, killing
    // the gateway the first click had just brought up.
    expect(postedModes()).toEqual([
      ['ports/2/mode', 'disabled'],
      ['ports/1/mode', 'tcp_bridge'],
      ['ports/2/mode', 'tcp_bridge'],
    ]);
    // Both ports ended up as gateways — the state the two clicks asked for.
    expect(device).toEqual({ port1Mode: 'tcp_bridge', port2Mode: 'tcp_bridge' });

    wrapper.unmount();
  });
});
