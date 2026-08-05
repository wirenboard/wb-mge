/**
 * Integration tests for Sniffer.vue.
 *
 * SNIF-I-001 — WS reconnect timer cleanup on stopCapture:
 *   Scenario A: stopCapture() clears the pending reconnect timer so connectWs is not called again.
 *   Scenario B: onUnmounted clears the reconnect timer — no orphan WebSocket is created.
 *
 * SNIF-I-002 — startCapture serial-open handling:
 *   The live sniffer is a WebSocket display overlay; it only needs the serial port open.
 *   Only the 'disabled' transport requires opening serial as 'passive'; all other transports
 *   ('tcp_bridge', 'passive') already have serial open and must NOT be switched.
 *   Scenario A: port in 'tcp_bridge' mode → NO switch (serial already open).
 *   Scenario B: port in 'disabled' mode → switch to 'passive' triggered (regression guard).
 *   Scenario C: port in 'passive' mode → NO switch.
 *   Scenario D: port in 'tcp_bridge' mode with cache overlay → NO switch.
 *   Scenario E: fetchInfo throws, info stays undefined → connectWs is still called immediately.
 *
 * SNIF-I-003 — virtualization windows the DOM:
 *   When many packets are captured, ALL are kept in the in-memory rows array (the heading
 *   shows the full count), but only a small window of rows is rendered as <tr class="sniff-row">.
 *   A spacer row (tr.sniff-spacer) absorbs the height of the off-screen rows below the window.
 *
 * SNIF-I-005 — the packet sender starts the capture and waits for it:
 *   Sending with the capture off used to show neither the request nor the reply, because the
 *   WebSocket is opened only by startCapture(). The send now starts the capture and — the part
 *   that matters — does not transmit until the socket is actually open, since startCapture()
 *   returns as soon as the socket has been REQUESTED. The wait is bounded, releasable and
 *   truthfully reported: a frame must never be cancelled or stranded by the sniffer, and the
 *   hint must never promise packets that cannot arrive.
 *   Scenario A: capture off → send starts it and holds the frame until ws.onopen fires.
 *   Scenario B: capture already running → the frame goes out at once, no second WebSocket.
 *   Scenario C: Stop pressed while the send waits → released at once, reported as unrecorded.
 *   Scenario D: the socket never opens → released by the timeout, reported as unrecorded.
 *   Scenario E: the WebSocket cannot even be constructed → the frame still goes out and the
 *     toolbar rolls back out of the running state it never reached.
 *
 * SNIF-I-006 — a resend goes to the port the frame was captured on:
 *   The resend now waits for the capture, and the port buttons stay live during that wait, so the
 *   filter can move between the click and the transmission. The destination is pinned when the
 *   row is read: replaying a port 1 frame onto port 2 would put it on a different bus.
 *
 * SNIF-I-004 — ring-buffer overflow scroll compensation:
 *   When the ring buffer overflows while the user is scrolled up (autoScroll=false), the
 *   dropped-oldest rows shift the rendered content up, so scrollTop is reduced by
 *   droppedVisible*rowHeight (clamped at 0). Uses the test-only `maxRows` prop to force overflow.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import type { Info } from '@/common/types';

// Minimal WebSocket mock.
// Each construction replaces MockWS.instance so tests can detect new WS creations.

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

// Shared info ref — mutated per test to simulate different port modes.

const sharedInfoRef = ref<Info | undefined>(undefined);
const fetchInfoMock = vi.fn().mockResolvedValue(undefined);

// Mocks — must be hoisted before the component import.

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
function makeInfo(
  rs485_1_mode: Info['rs485_1']['port_mode'],
  rs485_2_mode: Info['rs485_2']['port_mode'],
  rs485_1_cache = false,
  rs485_2_cache = false,
): Info {
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
    rs485_1: { is_busy: false, error_percentage: 0, server_connections_count: 0, port_mode: rs485_1_mode, cache_enabled: rs485_1_cache },
    rs485_2: { is_busy: false, error_percentage: 0, server_connections_count: 0, port_mode: rs485_2_mode, cache_enabled: rs485_2_cache },
    cache_modbus_port: 0,
    cache_modbus_server_enabled: false,
    cache_value_timeout_s: 0,
    psram_available: false,
    psram_size_kb: 0,
  };
}

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

    // fetchInfo must be called to refresh sidebar after stopCapture
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

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

    // fetchInfo must be called to refresh sidebar after stopCapture (triggered by unmount).
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    // Drain remaining timers.
    vi.runAllTimers();
    await flushPromises();
  });
});

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

  it('scenario A: port in tcp_bridge mode → NO switch (serial already open)', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 is in tcp_bridge mode — serial is already open, so no switch is needed.
    sharedInfoRef.value = makeInfo('tcp_bridge', 'disabled');

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

    // fetchInfo('low') must NOT be called — no mode switch occurred.
    expect(fetchInfoMock).not.toHaveBeenCalledWith('low');

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario B: port in disabled mode → switch to passive triggered (regression guard)', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 is disabled — serial must be opened as 'passive' before connecting the WS.
    sharedInfoRef.value = makeInfo('disabled', 'disabled');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();

    await captureBtn!.trigger('click');
    await flushPromises();

    expect(vi.mocked(apiMock)).toHaveBeenCalledWith(
      'ports/1/mode',
      expect.objectContaining({ method: 'POST', json: { mode: 'passive' } }),
    );

    await vi.advanceTimersByTimeAsync(600);
    await flushPromises();

    expect(MockWS.constructCount).toBeGreaterThanOrEqual(1);

    // fetchInfo must be called after the mode switch to refresh the sidebar.
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario C: port in passive mode → NO switch', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 is already in passive mode — serial is open, no switch needed.
    sharedInfoRef.value = makeInfo('passive', 'disabled');

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

    // fetchInfo('low') must NOT be called — no mode switch occurred.
    expect(fetchInfoMock).not.toHaveBeenCalledWith('low');

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario D: port in tcp_bridge mode with cache overlay → NO switch', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Port 1 has the cache overlay on over a tcp_bridge transport — serial is open, no switch.
    sharedInfoRef.value = makeInfo('tcp_bridge', 'disabled', true, false);

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

    // fetchInfo('low') must NOT be called — no mode switch occurred.
    expect(fetchInfoMock).not.toHaveBeenCalledWith('low');

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

describe('SNIF-I-005: the packet sender starts the capture and waits for it', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });
  const fetchMock = vi.fn();

  beforeEach(() => {
    MockWS.instance = null;
    MockWS.constructCount = 0;
    // info stays undefined → startCapture() skips the port-mode branch and connects at once.
    sharedInfoRef.value = undefined;
    vi.useFakeTimers();
    vi.stubGlobal('WebSocket', MockWS);
    // sendPacketToPort() posts with the global fetch — the send endpoint is not the `api` mock.
    fetchMock.mockReset();
    fetchMock.mockResolvedValue({ ok: true, json: async () => ({ sent: 8 }) });
    vi.stubGlobal('fetch', fetchMock);
    fetchInfoMock.mockClear();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
    vi.resetModules();
  });

  /** Open the sender popup from the toolbar. */
  async function openSender(wrapper: ReturnType<typeof mount>) {
    const btn = wrapper.findAll('button').find((b) => b.text().includes('Send packet'));
    expect(btn, 'Send packet button must be found').toBeDefined();
    await btn!.trigger('click');
    await flushPromises();
  }

  /** The popup hint shown when the frame went out with no capture recording it. */
  const NOT_RUNNING_HINT = 'Sent without a running capture, so the packets will not appear in the list';

  it('scenario A: capture off → send starts it and holds the frame until the socket is open', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Capture is off: the toolbar still offers Start.
    expect(wrapper.findAll('button').some((b) => b.text() === 'Start')).toBe(true);

    await openSender(wrapper);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // The capture was started by the send...
    expect(MockWS.constructCount).toBe(1);
    // ...and NOTHING has been transmitted yet, because the socket has not opened. This is the
    // whole point: without the wait the frame would go out here and be missed, exactly as before.
    expect(fetchMock).not.toHaveBeenCalled();

    // The socket opens → the capture is established → the frame is released.
    MockWS.instance!.onopen?.();
    await flushPromises();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    // The capture is left running, and the toolbar reflects it.
    expect(wrapper.findAll('button').some((b) => b.text() === 'Stop')).toBe(true);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario B: capture already running → the frame goes out at once, no second WebSocket', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();
    await captureBtn!.trigger('click');
    await flushPromises();
    MockWS.instance!.onopen?.();
    await wrapper.vm.$nextTick();

    await openSender(wrapper);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // No extra WebSocket, and no waiting: the running capture is used as-is.
    expect(MockWS.constructCount).toBe(1);
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario C: Stop while the send waits releases the frame at once, reported as unrecorded', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    await openSender(wrapper);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // The socket has been requested but never opened, so the frame is still being held.
    expect(MockWS.constructCount).toBe(1);
    expect(fetchMock).not.toHaveBeenCalled();

    // The user presses Stop while the send waits. Nothing will connect now.
    const stopBtn = wrapper.findAll('button').find((b) => b.text() === 'Stop');
    expect(stopBtn, 'Stop button must be found').toBeDefined();
    await stopBtn!.trigger('click');
    await flushPromises();

    // Released immediately — the fake clock has not moved, so this cannot be the 5 s timeout.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    // And the popup admits the capture is off instead of claiming it started one.
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(NOT_RUNNING_HINT);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario D: the socket never opens → the timeout still sends the frame, reported as unrecorded', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    await openSender(wrapper);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    expect(fetchMock).not.toHaveBeenCalled();

    // CAPTURE_READY_TIMEOUT_MS (5 s) elapses with ws.onopen never firing.
    await vi.advanceTimersByTimeAsync(5001);
    await flushPromises();

    // A send must never hang because the socket cannot be established.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(NOT_RUNNING_HINT);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('scenario E: the WebSocket cannot be constructed → the frame goes out, the toolbar rolls back', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // `new WebSocket(url)` throws synchronously on a malformed URL or on mixed content.
    class ThrowingWebSocket {
      constructor() {
        throw new Error('SecurityError: mixed content');
      }
    }
    vi.stubGlobal('WebSocket', ThrowingWebSocket);
    const warn = vi.spyOn(console, 'warn').mockImplementation(() => {});

    await openSender(wrapper);
    await wrapper.find('.sniffer-sender-foot-send').trigger('click');
    await flushPromises();

    // Sending a frame never depended on the sniffer socket, and must not start to now.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');
    expect(warn).toHaveBeenCalled();
    expect(wrapper.find('.sniffer-sender-hint').text()).toBe(NOT_RUNNING_HINT);
    // The failure is not shown as a send error — the send itself succeeded.
    expect(wrapper.find('.sniffer-sender-error').exists()).toBe(false);
    // The toolbar is back on Start: the view does not sit in a running state it never reached.
    expect(wrapper.findAll('button').some((b) => b.text() === 'Start')).toBe(true);

    warn.mockRestore();
    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });
});

describe('SNIF-I-006: a resend goes to the port the frame was captured on', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });
  const fetchMock = vi.fn();

  beforeEach(() => {
    MockWS.instance = null;
    MockWS.constructCount = 0;
    sharedInfoRef.value = undefined;
    vi.useFakeTimers();
    vi.stubGlobal('WebSocket', MockWS);
    fetchMock.mockReset();
    fetchMock.mockResolvedValue({ ok: true, json: async () => ({ sent: 6 }) });
    vi.stubGlobal('fetch', fetchMock);
    fetchInfoMock.mockClear();
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.unstubAllGlobals();
    vi.resetModules();
  });

  it('the port filter moving while the resend waits does not redirect the frame', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Capture one resendable frame (valid CRC, non-empty payload) on port 1.
    await wrapper.findAll('button').find((b) => b.text() === 'Start')!.trigger('click');
    await flushPromises();
    MockWS.instance!.onopen?.();
    await wrapper.vm.$nextTick();
    MockWS.instance!.onmessage?.({
      data: JSON.stringify({
        id: 1,
        type: 'packet',
        port: 1,
        function: 3,
        slave_id: 1,
        sender: 'master',
        crc_valid: true,
        raw: '0103000A0002',
        size: 6,
        timestamp_us: 1000,
      }),
    });
    await vi.runAllTimersAsync();
    await flushPromises();

    // Stop the capture, so the resend has to start it again — that is the wait during which the
    // user can still press a port button.
    await wrapper.findAll('button').find((b) => b.text() === 'Stop')!.trigger('click');
    await flushPromises();

    // Select the captured row and resend it.
    await wrapper.find('tr.sniff-row').trigger('click');
    await flushPromises();
    const resendBtn = wrapper.findAll('button').find((b) => b.text().includes('Resend'));
    expect(resendBtn, 'Resend button must be found').toBeDefined();
    await resendBtn!.trigger('click');
    await flushPromises();

    // Held until the capture is established — nothing on the wire yet.
    expect(fetchMock).not.toHaveBeenCalled();

    // Only the Resend button is disabled during the wait, so the user can still switch ports.
    const port2 = wrapper.findAll('button').find((b) => b.text() === 'Port 2');
    expect(port2, 'Port 2 button must be found').toBeDefined();
    await port2!.trigger('click');
    await flushPromises();

    // The capture comes up and the held frame is released.
    MockWS.instance!.onopen?.();
    await flushPromises();

    // It must go back to port 1, where it was captured — port 2 is a different bus with
    // different devices, and replaying a write there is a real hazard.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toBe('/ports/1/send');

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });
});

describe('SNIF-I-003: virtualization windows the DOM', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    MockWS.instance = null;
    MockWS.constructCount = 0;
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

  it('keeps all packets in memory but renders only a windowed slice of rows', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Start capture so the WS is created and onmessage is wired up.
    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();
    await captureBtn!.trigger('click');
    await flushPromises();

    const ws = MockWS.instance!;
    expect(ws, 'WebSocket must have been created').not.toBeNull();
    ws.onopen?.();
    await wrapper.vm.$nextTick();

    // Feed N=60 valid packet messages on the default port (1) so filteredRows includes them.
    const N = 60;
    for (let id = 1; id <= N; id += 1) {
      const msg = {
        id,
        type: 'packet',
        port: 1,
        function: 3,
        slave_id: 1,
        sender: 'master',
        crc_valid: true,
        raw: '0103000A0002',
        size: 6,
        timestamp_us: 1000 * id,
      };
      ws.onmessage?.({ data: JSON.stringify(msg) });
    }

    // Ingestion is buffered in `pending` and flushed on requestAnimationFrame. happy-dom backs
    // rAF with setImmediate, so drain ALL pending timers (rather than a fixed time advance) to
    // deterministically run the flush, then drain microtasks for the resulting DOM update.
    await vi.runAllTimersAsync();
    await wrapper.vm.$nextTick();
    await flushPromises();

    // All packets are kept in memory: the heading counter (first <b> in .heading-stats) renders
    // rows.length. Assert it exactly so the check cannot pass on an incidental "60" elsewhere.
    expect(wrapper.find('.heading-stats b').text()).toBe(String(N));

    // The DOM is windowed: only a small slice of rows is rendered, not all 60.
    // In happy-dom clientHeight/offsetHeight are 0, so rowHeight stays the 29 fallback and
    // viewportH=0 → visibleCount = OVERSCAN*2 = 20, so ~20 rows render (definitely < 60).
    const rendered = wrapper.findAll('tr.sniff-row').length;
    expect(rendered).toBeGreaterThan(0);
    expect(rendered).toBeLessThan(N);

    // A bottom spacer row exists because padBottom > 0 (off-screen rows below the window).
    expect(wrapper.findAll('tr.sniff-spacer').length).toBeGreaterThanOrEqual(1);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });
});

/**
 * Exercises the flushPending() branch that runs ONLY when the ring buffer overflows AND the
 * user is NOT following the tail (autoScroll=false): dropping the oldest rows shifts the
 * rendered content up, so the scroll position is compensated by `droppedVisible * rowHeight`
 * (the filter-aware count of dropped rows), clamped at 0.
 *
 * The production cap (50000) is impractical to reach in a test, so the component accepts an
 * optional `maxRows` prop (default 50000, identical to the old MAX_ROWS constant) which we set
 * to a small value here. In happy-dom layout is 0 (clientHeight/offsetHeight=0), so rowHeight
 * stays the 29px fallback; we drive autoScroll=false directly and seed a positive scrollTop on
 * the table-wrap element, then assert the compensation arithmetic actually ran.
 */
describe('SNIF-I-004: ring-buffer overflow scroll compensation', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    MockWS.instance = null;
    MockWS.constructCount = 0;
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

  /** Mount, start capture, open the WS, and return the live MockWS + the component vm. */
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  async function startCapture(wrapper: ReturnType<typeof mount>): Promise<{ ws: MockWS; vm: any }> {
    const captureBtn = wrapper.findAll('button').find((b) => b.text() === 'Start');
    expect(captureBtn, 'Start button must be found').toBeDefined();
    await captureBtn!.trigger('click');
    await flushPromises();
    const ws = MockWS.instance!;
    expect(ws, 'WebSocket must have been created').not.toBeNull();
    ws.onopen?.();
    await wrapper.vm.$nextTick();
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    return { ws, vm: wrapper.vm as any };
  }

  /** Feed `count` valid port-1 packets through the WS onmessage handler. */
  function feedPackets(ws: MockWS, count: number): void {
    for (let id = 1; id <= count; id += 1) {
      const msg = {
        id,
        type: 'packet',
        port: 1,
        function: 3,
        slave_id: 1,
        sender: 'master',
        crc_valid: true,
        raw: '0103000A0002',
        size: 6,
        timestamp_us: 1000 * id,
      };
      ws.onmessage?.({ data: JSON.stringify(msg) });
    }
  }

  it('shifts scrollTop up by droppedVisible*rowHeight when the buffer overflows while scrolled up', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    // Small cap so feeding 8 packets overflows the 5-row ring buffer (3 dropped).
    const wrapper = mount(Sniffer, { props: { maxRows: 5 }, global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const { ws, vm } = await startCapture(wrapper);

    // Drive the not-following-tail state: seed a positive scrollTop on the wrap element and
    // force autoScroll=false (in happy-dom the onScroll distance math would otherwise re-pin it).
    const el = vm.tableWrap as HTMLElement;
    el.scrollTop = 1000;
    vm.autoScroll = false;
    await wrapper.vm.$nextTick();

    // rowHeight is the 29px fallback in happy-dom (no layout). Capture it to assert the math.
    const rowHeight = vm.rowHeight as number;
    expect(rowHeight).toBeGreaterThan(0);

    // Feed 8 port-1 packets → length 8 > cap 5 → 3 oldest dropped, all pass the port-1 filter.
    feedPackets(ws, 8);

    // Drain the rAF-backed flush, then the nextTick that applies the scroll compensation.
    await vi.runAllTimersAsync();
    await wrapper.vm.$nextTick();
    await flushPromises();

    // Ring buffer capped at maxRows: the heading counter renders rows.length, which is the
    // TRIMMED buffer length (5), proving the 8→5 overflow trim ran.
    expect(wrapper.find('.heading-stats b').text()).toBe('5');
    const rendered = wrapper.findAll('tr.sniff-row').length;
    expect(rendered).toBe(5); // only the 5 retained rows render

    // The compensation branch ran: scrollTop dropped by droppedVisible(3) * rowHeight, clamped ≥ 0.
    const expected = Math.max(0, 1000 - 3 * rowHeight);
    expect(el.scrollTop).toBe(expected);
    expect(vm.scrollTop).toBe(expected); // the reactive ref was synced to the DOM
    expect(expected).toBeLessThan(1000); // it actually shifted up
    expect(el.scrollTop).toBeGreaterThanOrEqual(0);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });

  it('clamps the compensated scrollTop at 0 (never negative)', async () => {
    const { default: Sniffer } = await import('@/views/Sniffer.vue');

    const wrapper = mount(Sniffer, { props: { maxRows: 5 }, global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const { ws, vm } = await startCapture(wrapper);

    const el = vm.tableWrap as HTMLElement;
    // Seed a tiny scrollTop so the shift (3 * 29 = 87) would go negative → must clamp to 0.
    el.scrollTop = 10;
    vm.autoScroll = false;
    await wrapper.vm.$nextTick();

    feedPackets(ws, 8);

    await vi.runAllTimersAsync();
    await wrapper.vm.$nextTick();
    await flushPromises();

    // 10 - 3*29 < 0 → clamped to 0.
    expect(el.scrollTop).toBe(0);
    expect(vm.scrollTop).toBe(0);

    wrapper.unmount();
    vi.runAllTimers();
    await flushPromises();
  });
});
