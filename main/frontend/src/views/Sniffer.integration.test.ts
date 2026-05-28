/**
 * Integration tests for Sniffer.vue.
 *
 * SNIF-I-001 — WS reconnect timer cleanup on stopCapture:
 *   Scenario A: stopCapture() clears the pending reconnect timer so connectWs is not called again.
 *   Scenario B: onUnmounted clears the reconnect timer — no orphan WebSocket is created.
 *
 * SNIF-I-002 — startCapture auto-switch to sniffer mode:
 *   Scenario A: port in 'tcp_bridge' mode → auto-switch to sniffer triggered.
 *   Scenario B: port in 'disabled' mode → auto-switch to sniffer triggered (regression guard).
 *   Scenario C: port in 'sniffer' mode → NO auto-switch (api NOT called with ports/N/mode).
 *   Scenario D: port in 'cache_bus' mode → NO auto-switch.
 *   Scenario E: fetchInfo throws, info stays undefined → connectWs is still called immediately.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import type { Info } from '@/common/types';

// ---------------------------------------------------------------------------
// Minimal WebSocket mock.
// Each construction replaces MockWS.instance so tests can detect new WS creations.
// ---------------------------------------------------------------------------

class MockWS {
  static instance: MockWS | null = null;
  static constructCount = 0;

  onopen: (() => void) | null = null;
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  onmessage: ((ev: any) => void) | null = null;
  onclose: (() => void) | null = null;

  constructor(public url: string) {
    MockWS.instance = this;
    MockWS.constructCount += 1;
  }

  close() {
    // Simulate browser WS: close() triggers onclose asynchronously.
    // Use synchronous call here so tests can control the sequence.
    this.onclose?.();
  }

  send() {}
}

// ---------------------------------------------------------------------------
// Shared info ref — mutated per test to simulate different port modes.
// ---------------------------------------------------------------------------

const sharedInfoRef = ref<Info | undefined>(undefined);
const fetchInfoMock = vi.fn().mockResolvedValue(undefined);

// ---------------------------------------------------------------------------
// Mocks — must be hoisted before the component import.
// ---------------------------------------------------------------------------

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: sharedInfoRef, fetchInfo: fetchInfoMock }),
}));

vi.mock('@/utils/api', () => ({
  api: vi.fn().mockResolvedValue({}),
}));

vi.mock('@/common/hostname', () => ({
  useHostname: () => ({ hostname: ref('') }),
  setHostname: vi.fn(),
}));

vi.mock('@/common/settings', () => ({
  useSettings: () => ({
    initData: ref(null),
    data: ref(null),
    isChanged: () => false,
    refresh: vi.fn().mockResolvedValue(undefined),
  }),
}));

// Stub @unhead/vue so Heading.vue's injectHead() / useHead() do not throw.
vi.mock('@unhead/vue', () => ({
  injectHead: () => ({}),
  useHead: vi.fn(),
  createUnhead: vi.fn(() => ({})),
  headSymbol: Symbol('head'),
}));

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function makeRouter() {
  return createRouter({
    history: createMemoryHistory(),
    routes: [
      { path: '/', component: { template: '<div/>' } },
      { path: '/logout', component: { template: '<div/>' } },
    ],
  });
}

/** Minimal Info object with only the fields startCapture() reads. */
function makeInfo(rs485_1_mode: Info['rs485_1']['port_mode'], rs485_2_mode: Info['rs485_2']['port_mode']): Info {
  return {
    device_name: '',
    serial_num: 0,
    firmware: '',
    hardware: '',
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
    rs485_1: { is_busy: false, error_percentage: 0, server_connections_count: 0, port_mode: rs485_1_mode },
    rs485_2: { is_busy: false, error_percentage: 0, server_connections_count: 0, port_mode: rs485_2_mode },
    cache_modbus_port: 0,
    cache_modbus_server_enabled: false,
    cache_value_timeout_s: 0,
    psram_available: false,
    psram_size_kb: 0,
  };
}

// ---------------------------------------------------------------------------
// SNIF-I-001: WS reconnect timer cleanup on stopCapture
// ---------------------------------------------------------------------------

describe('SNIF-I-001: WS reconnect timer cleanup on stopCapture', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    // Reset MockWS state before each test.
    MockWS.instance = null;
    MockWS.constructCount = 0;
    // SNIF-I-001 does not depend on info.value — keep it undefined.
    sharedInfoRef.value = undefined;
    vi.useFakeTimers();
    vi.stubGlobal('WebSocket', MockWS);
    fetchInfoMock.mockClear();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
    vi.resetModules();
  });

  it('scenario A: stopCapture() clears the pending reconnect timer — connectWs not re-called', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Click the Start button to initiate capture (running=true, connectWs called).
    // Find the Start/Stop button — it is the last Button component rendered in the heading.
    // The Sniffer template renders: Export CSV button, Clear button, then Start/Stop button.
    const allBtns = wrapper.findAll('button');
    // The start/stop button text is "Start" when running=false.
    const captureBtn = allBtns.find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();

    await captureBtn!.trigger('click');
    await flushPromises();

    // A WebSocket should have been created.
    expect(MockWS.constructCount).toBe(1);
    const firstWs = MockWS.instance!;
    expect(firstWs).not.toBeNull();

    // Simulate WS open → wsStatus becomes 'connected'.
    firstWs.onopen?.();
    await wrapper.vm.$nextTick();

    // Simulate WS close while running=true → onclose fires, reconnect timer is set.
    // We call onclose directly (bypassing firstWs.close()) to avoid calling onclose twice.
    firstWs.onclose?.();
    await wrapper.vm.$nextTick();

    // wsStatus should now be 'reconnecting' (running=true and ws closed).
    // The component shows the stop button text when running.
    const stopBtn = wrapper.findAll('button').find((b) => b.text() === 'Stop');
    expect(stopBtn, 'Stop button must be present while running').toBeDefined();

    // Record the WebSocket construct count before calling stop.
    const countBeforeStop = MockWS.constructCount;

    // Click Stop → stopCapture() is called → reconnectTimer is cleared.
    await stopBtn!.trigger('click');
    await flushPromises();

    // Advance time well past the 2000ms reconnect delay.
    vi.advanceTimersByTime(3000);
    await flushPromises();

    // No new WebSocket should have been created after stop.
    expect(MockWS.constructCount).toBe(countBeforeStop);

    wrapper.unmount();
    // Drain any remaining timers after unmount to avoid leaking into subsequent tests.
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario B: onUnmounted clears the reconnect timer — no orphan WebSocket after unmount', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Click Start.
    const allBtns = wrapper.findAll('button');
    const captureBtn = allBtns.find((b) => b.text() === 'Start');
    expect(captureBtn).toBeDefined();
    await captureBtn!.trigger('click');
    await flushPromises();

    expect(MockWS.constructCount).toBe(1);
    const firstWs = MockWS.instance!;

    // Simulate WS open.
    firstWs.onopen?.();
    await wrapper.vm.$nextTick();

    // Simulate WS close while running=true → reconnect timer is scheduled.
    firstWs.onclose?.();
    await wrapper.vm.$nextTick();

    // Record the WS count before unmounting.
    const countBeforeUnmount = MockWS.constructCount;

    // Unmount the component → onUnmounted calls stopCapture() → timer is cleared.
    wrapper.unmount();
    await flushPromises();

    // Advance time well past the 2000ms reconnect delay.
    vi.advanceTimersByTime(3000);
    await flushPromises();

    // No new WebSocket should have been created after unmount.
    expect(MockWS.constructCount).toBe(countBeforeUnmount);

    // Drain remaining timers.
    vi.runAllTimers();
    await flushPromises();
  });
});

// ---------------------------------------------------------------------------
// SNIF-I-002: startCapture auto-switch to sniffer mode
// ---------------------------------------------------------------------------

// Import the api mock once at module level — vi.mock hoists the factory so
// this import always resolves to the mocked version.
import { api as apiMock } from '@/utils/api';

describe('SNIF-I-002: startCapture auto-switch', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    MockWS.instance = null;
    MockWS.constructCount = 0;
    sharedInfoRef.value = undefined;
    vi.useFakeTimers();
    vi.stubGlobal('WebSocket', MockWS);
    // Reset api mock call history before each test.
    vi.mocked(apiMock).mockClear();
    fetchInfoMock.mockClear();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
    vi.resetModules();
  });

  it('scenario A: port in tcp_bridge mode → auto-switch to sniffer triggered', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 is in tcp_bridge mode — should trigger auto-switch.
    sharedInfoRef.value = makeInfo('tcp_bridge', 'disabled');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();

    await captureBtn!.trigger('click');
    // Allow the api call (mode switch) to complete.
    await flushPromises();

    // Verify that api was called with the mode-switch endpoint for port 1.
    expect(vi.mocked(apiMock)).toHaveBeenCalledWith(
      'ports/1/mode',
      expect.objectContaining({ method: 'POST', json: { mode: 'sniffer' } }),
    );

    // Advance past the 500ms PORT_MODE_SWITCH_DELAY_MS so connectWs is called.
    await vi.advanceTimersByTimeAsync(600);
    await flushPromises();

    // WebSocket should have been created after the delay.
    expect(MockWS.constructCount).toBeGreaterThanOrEqual(1);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario B: port in disabled mode → auto-switch to sniffer triggered (regression guard)', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 is disabled — should trigger auto-switch just as before the fix.
    sharedInfoRef.value = makeInfo('disabled', 'disabled');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();

    await captureBtn!.trigger('click');
    await flushPromises();

    expect(vi.mocked(apiMock)).toHaveBeenCalledWith(
      'ports/1/mode',
      expect.objectContaining({ method: 'POST', json: { mode: 'sniffer' } }),
    );

    await vi.advanceTimersByTimeAsync(600);
    await flushPromises();

    expect(MockWS.constructCount).toBeGreaterThanOrEqual(1);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario C: port in sniffer mode → NO auto-switch', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 is already in sniffer mode — no switch needed.
    sharedInfoRef.value = makeInfo('sniffer', 'disabled');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();

    await captureBtn!.trigger('click');
    await flushPromises();

    // api should NOT have been called with a mode-switch endpoint.
    const modeSwitchCalls = vi.mocked(apiMock).mock.calls.filter(
      (args) => typeof args[0] === 'string' && args[0].includes('/mode'),
    );
    expect(modeSwitchCalls).toHaveLength(0);

    // WS should be created immediately (no 500ms delay).
    expect(MockWS.constructCount).toBeGreaterThanOrEqual(1);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario D: port in cache_bus mode → NO auto-switch', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 is in cache_bus mode — no switch needed.
    sharedInfoRef.value = makeInfo('cache_bus', 'disabled');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();

    await captureBtn!.trigger('click');
    await flushPromises();

    // api should NOT have been called with a mode-switch endpoint.
    const modeSwitchCalls = vi.mocked(apiMock).mock.calls.filter(
      (args) => typeof args[0] === 'string' && args[0].includes('/mode'),
    );
    expect(modeSwitchCalls).toHaveLength(0);

    // WS should be created immediately (no 500ms delay).
    expect(MockWS.constructCount).toBeGreaterThanOrEqual(1);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario E: fetchInfo throws, info stays undefined → connectWs is still called (MockWS created)', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // info.value remains undefined — fetchInfo will throw instead of populating it.
    // sharedInfoRef.value is already undefined from beforeEach.
    fetchInfoMock.mockRejectedValueOnce(new Error('network error'));

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();

    await captureBtn!.trigger('click');
    // Allow the rejected fetchInfo promise to settle.
    await flushPromises();
    // connectWs is called synchronously — no timer delay in this path.

    // connectWs must have been called despite fetchInfo throwing.
    expect(MockWS.constructCount).toBeGreaterThanOrEqual(1);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });
});
