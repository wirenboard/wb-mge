/**
 * Integration tests for Sniffer.vue.
 *
 * SNIF-I-001 — WS reconnect timer cleanup on stopCapture:
 *   Scenario A: stopCapture() clears the pending reconnect timer so connectWs is not called again.
 *   Scenario B: onUnmounted clears the reconnect timer — no orphan WebSocket is created.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';

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

  send(_data: string) {}
}

// ---------------------------------------------------------------------------
// Mocks — must be hoisted before the component import.
// ---------------------------------------------------------------------------

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: ref(undefined) }),
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

// ---------------------------------------------------------------------------
// SNIF-I-001: WS reconnect timer cleanup on stopCapture
// ---------------------------------------------------------------------------

describe('SNIF-I-001: WS reconnect timer cleanup on stopCapture', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    // Reset MockWS state before each test.
    MockWS.instance = null;
    MockWS.constructCount = 0;
    vi.useFakeTimers();
    vi.stubGlobal('WebSocket', MockWS);
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
