/**
 * Integration tests for RegisterMap.vue.
 *
 * RM-I-001 — port-initialization guard: verifies that the `portsInitialized` flag prevents
 *             subsequent info-poll updates from overwriting user-editable fields.
 *
 * RM-I-003 — toggleCaching enable path: verifies that clicking "Enable caching" calls
 *             api('ports/N/mode') for the selected port only, then triggers fetchEntries.
 *
 * Rendering note: the settings panel is gated by `v-if="!cacheEnabled"` / `v-else-if="loading"`,
 * so flushPromises() after mount is required to let onMounted settle before inspecting the DOM.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import type { Info } from '@/common/types';
import { api } from '@/utils/api';

// ---------------------------------------------------------------------------
// Shared reactive info ref — mutated per test scenario to simulate polls.
// ---------------------------------------------------------------------------
const infoRef = ref<Info | undefined>(undefined);

// ---------------------------------------------------------------------------
// Mocks — hoisted before any component import by Vitest's vi.mock hoisting.
// ---------------------------------------------------------------------------

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef }),
}));

vi.mock('@/utils/api', () => ({
  // Provide a minimal resolved value so onMounted fetch calls do not throw.
  api: vi.fn().mockResolvedValue({ d: [] }),
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

/** Build a minimal Info object — only the fields consumed by the watch are set. */
function makeInfo(opts: {
  port1Mode: 'cache_bus' | 'disabled';
  port2Mode: 'cache_bus' | 'disabled';
  tcpPort: number;
  timeout: number;
  tcpServerEnabled?: boolean;
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
    },
    rs485_2: {
      is_busy: false,
      error_percentage: 0,
      server_connections_count: 0,
      port_mode: opts.port2Mode,
    },
    cache_modbus_port: opts.tcpPort,
    cache_modbus_server_enabled: opts.tcpServerEnabled ?? true,
    cache_value_timeout_s: opts.timeout,
    psram_available: false,
    psram_size_kb: 0,
  };
}

/** Read the numeric value of the first input matching the given CSS selector. */
function inputValue(wrapper: ReturnType<typeof mount>, selector: string): number {
  const el = wrapper.find(selector).element as HTMLInputElement;
  return Number(el.value);
}

/** Return true if the nth port-tag button (1-based) has the 'active' CSS class. */
function portActive(wrapper: ReturnType<typeof mount>, nth: 1 | 2): boolean {
  const buttons = wrapper.findAll('.rsp-port-tag');
  expect(buttons.length, `Expected at least ${nth} .rsp-port-tag buttons to be rendered`).toBeGreaterThanOrEqual(nth);
  return buttons[nth - 1].classes().includes('active');
}

/** Return the checked state of a Switch component rendered as <input type="checkbox" :id="id">. */
function switchChecked(wrapper: ReturnType<typeof mount>, id: string): boolean {
  const el = wrapper.find(`#${id}`).element as HTMLInputElement;
  return el.checked;
}

/**
 * Create a minimal stub vue-router instance.
 * Sidebar.vue calls useRoute() and useRouter(), so a router plugin is required.
 * The /logout route is included because Sidebar.vue navigates there on sign-out.
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

// ---------------------------------------------------------------------------
// Test suite
// ---------------------------------------------------------------------------

describe('RM-I-001: RegisterMap port-initialization guard', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    // Reset info and module registry so each test gets a fresh portsInitialized = false.
    infoRef.value = undefined;
    vi.resetModules();
  });

  it('scenario 1+2: first poll initialises fields; second poll does not overwrite them', async () => {
    // Dynamic import after resetModules ensures a fresh module instance.
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // --- Scenario 1: first poll sets port1=cache_bus, tcpPort=1234, timeout=30, tcpServerEnabled=true ---
    // Setting infoRef before mount ensures:
    //   - cacheEnabled === true → the settings panel branch is reached
    //   - the watch({ immediate: true }) fires synchronously during setup()
    infoRef.value = makeInfo({
      port1Mode: 'cache_bus',
      port2Mode: 'disabled',
      tcpPort: 1234,
      timeout: 30,
      tcpServerEnabled: true,
    });

    const wrapper = mount(RegisterMap, {
      global: { plugins: [i18n, makeRouter()] },
    });

    // flushPromises() lets the onMounted async fetchEntries() resolve,
    // which sets loading = false and causes the settings panel to render.
    await flushPromises();

    // cacheTcpPort: data-testid="cache-tcp-port" in the TCP MODBUS section
    expect(inputValue(wrapper, '[data-testid="cache-tcp-port"]')).toBe(1234);

    // valueTimeout: data-testid="value-timeout" in the CACHING section
    expect(inputValue(wrapper, '[data-testid="value-timeout"]')).toBe(30);

    // listenPort1 button should be active; listenPort2 should not
    expect(portActive(wrapper, 1)).toBe(true);
    expect(portActive(wrapper, 2)).toBe(false);

    // tcpServeEnabled should reflect the first poll value (true)
    expect(switchChecked(wrapper, 'rsp-tcp-serve')).toBe(true);

    // --- Scenario 2: second poll with completely different values (tcpServerEnabled=false) ---
    infoRef.value = makeInfo({
      port1Mode: 'disabled',
      port2Mode: 'cache_bus',
      tcpPort: 9999,
      timeout: 99,
      tcpServerEnabled: false,
    });
    await flushPromises();

    // All fields must remain at the FIRST poll values — portsInitialized guard prevents overwrite.
    expect(inputValue(wrapper, '[data-testid="cache-tcp-port"]')).toBe(1234);
    expect(inputValue(wrapper, '[data-testid="value-timeout"]')).toBe(30);
    expect(portActive(wrapper, 1)).toBe(true);
    expect(portActive(wrapper, 2)).toBe(false);

    // Verify tcpServeEnabled was not overwritten by the second poll (must still be true).
    expect(switchChecked(wrapper, 'rsp-tcp-serve')).toBe(true);

    wrapper.unmount();
  });

  it('scenario 3: double-true guard — both ports cache_bus on first poll forces listenPort2 to false', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Both ports report 'cache_bus'; the component must clamp listenPort2 to false.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'cache_bus', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, {
      global: { plugins: [i18n, makeRouter()] },
    });
    await flushPromises();

    expect(portActive(wrapper, 1)).toBe(true);
    // Must be false even though info reports 'cache_bus' for port 2.
    expect(portActive(wrapper, 2)).toBe(false);

    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-003: toggleCaching — enable path
// ---------------------------------------------------------------------------
/**
 * Integration test RM-I-003: RegisterMap toggleCaching — enable path.
 *
 * Verifies that clicking the "Enable caching" button (visible when cacheEnabled=false)
 * calls api('ports/N/mode', { method: 'POST', json: { mode: 'cache_bus' } }) for exactly
 * the ports selected in the Settings panel, and then triggers fetchEntries (api('cache/json')).
 *
 * Three scenarios:
 *   A. listenPort1=true,  listenPort2=false → only ports/1/mode + cache/json
 *   B. listenPort1=false, listenPort2=true  → only ports/2/mode + cache/json
 *   C. listenPort1=false, listenPort2=false → ports/1/mode (default) + cache/json
 *
 * Setup strategy:
 *   - infoRef is set BEFORE mount so the immediate watch initialises listenPort1/listenPort2.
 *   - After mount + flushPromises the portsInitialized guard is set, so subsequent
 *     infoRef changes no longer touch the listen-port refs.
 *   - infoRef is then mutated to have both ports 'disabled' so cacheEnabled becomes false,
 *     which renders the "Enable caching" button and puts toggleCaching() on the enable path.
 *   - The api mock is reconfigured so that the ports/N/mode POST call also updates infoRef
 *     to reflect 'cache_bus', making cacheEnabled true before fetchEntries() runs, which
 *     allows api('cache/json') to be invoked.
 *   - api mock calls are cleared before the button click to isolate assertions.
 */
describe('RM-I-003: toggleCaching enable', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    vi.resetModules();
    // Restore the default mock implementation for each test (resetModules clears module
    // state but not mock implementations — reset to a known baseline).
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('scenario A: listenPort1=true, listenPort2=false — only ports/1/mode called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // First poll: port1=cache_bus so listenPort1 is initialised to true.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Mutate info so cacheEnabled becomes false (both ports disabled).
    // portsInitialized guard ensures listenPort1 stays true, listenPort2 stays false.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
    await flushPromises();

    // Reconfigure the api mock: when ports/1/mode is POSTed, update infoRef to reflect
    // the new mode so that cacheEnabled becomes true before fetchEntries() runs.
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/1/mode') {
        infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
      }
      return { d: [] } as never;
    });

    // mockClear() clears call history only; the mockImplementation above is preserved.
    // Clear recorded calls so only the toggle-click calls are asserted.
    vi.mocked(api).mockClear();

    // Click the "Enable caching" button — rendered in .rm-off when cacheEnabled=false.
    await wrapper.find('.rm-off button').trigger('click');
    await flushPromises();

    const apiMock = vi.mocked(api);

    // ports/1/mode must be called with mode: 'cache_bus'
    expect(apiMock).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });

    // ports/2/mode must NOT be called
    expect(apiMock).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    // cache/json must be called (fetchEntries)
    expect(apiMock).toHaveBeenCalledWith('cache/json');

    wrapper.unmount();
  });

  it('scenario B: listenPort2=true, listenPort1=false — only ports/2/mode called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // First poll: port2=cache_bus so listenPort2 is initialised to true.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'cache_bus', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Mutate info so cacheEnabled becomes false; listenPort2 stays true via guard.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
    await flushPromises();

    // Reconfigure mock: ports/2/mode POST updates infoRef so cacheEnabled turns true.
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/2/mode') {
        infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'cache_bus', tcpPort: 504, timeout: 60 });
      }
      return { d: [] } as never;
    });

    // mockClear() clears call history only; the mockImplementation above is preserved.
    vi.mocked(api).mockClear();

    await wrapper.find('.rm-off button').trigger('click');
    await flushPromises();

    const apiMock = vi.mocked(api);

    // ports/1/mode must NOT be called
    expect(apiMock).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());

    // ports/2/mode must be called with mode: 'cache_bus'
    expect(apiMock).toHaveBeenCalledWith('ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } });

    // cache/json must be called (fetchEntries)
    expect(apiMock).toHaveBeenCalledWith('cache/json');

    wrapper.unmount();
  });

  it('scenario C: both false (edge case) — defaults to ports/1/mode, ports/2/mode not called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // First poll: both ports disabled → listenPort1=false, listenPort2=false after watch.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // cacheEnabled is already false; no need to mutate infoRef again.
    // Reconfigure mock: ports/1/mode POST updates infoRef so cacheEnabled turns true
    // (default-port-1 fallback branch inside toggleCaching).
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/1/mode') {
        infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
      }
      return { d: [] } as never;
    });

    // mockClear() clears call history only; the mockImplementation above is preserved.
    vi.mocked(api).mockClear();

    await wrapper.find('.rm-off button').trigger('click');
    await flushPromises();

    const apiMock = vi.mocked(api);

    // ports/1/mode must be called with mode: 'cache_bus' (default fallback)
    expect(apiMock).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });

    // ports/2/mode must NOT be called
    expect(apiMock).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    // cache/json must be called (fetchEntries)
    expect(apiMock).toHaveBeenCalledWith('cache/json');

    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-04: toggleCaching — disable path
// ---------------------------------------------------------------------------
/**
 * Verifies that clicking the caching toggle when cacheEnabled=true (disable path):
 *   A. calls ports/1/mode disabled only (port1 in cache_bus, port2 disabled)
 *   B. calls both ports/1 and ports/2 disabled (both in cache_bus)
 *   C. sets error.value on API failure (rendered via .rm-error-wrap)
 */
describe('RM-I-04: toggleCaching — disable path', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('scenario A: only port1 in cache_bus → only ports/1/mode disabled called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Clear call history — isolate only the toggle-click calls
    vi.mocked(api).mockClear();

    // Click the caching toggle label (calls toggleCaching() on the disable path)
    await wrapper.find('.rm-caching-toggle').trigger('click');
    await flushPromises();

    // ports/1/mode must be called with mode: 'disabled'
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'disabled' } });

    // ports/2/mode must NOT be called (port2 was already disabled)
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    wrapper.unmount();
  });

  it('scenario B: both ports in cache_bus → both ports/1 and ports/2 disabled', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'cache_bus', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    vi.mocked(api).mockClear();

    await wrapper.find('.rm-caching-toggle').trigger('click');
    await flushPromises();

    // Both ports must be disabled
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'disabled' } });
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/2/mode', { method: 'POST', json: { mode: 'disabled' } });

    wrapper.unmount();
  });

  it('scenario C: api throws → error.value set, .rm-error-wrap rendered', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Make the next API call throw to simulate a network error
    vi.mocked(api).mockRejectedValueOnce(new Error('Network error'));

    await wrapper.find('.rm-caching-toggle').trigger('click');
    await flushPromises();

    // cacheEnabled is still true (infoRef not updated), loading=false, error is set
    // → .rm-error-wrap should be rendered
    expect(wrapper.find('.rm-error-wrap').exists()).toBe(true);
    expect(wrapper.find('.rm-error-wrap').text()).toContain('Network error');

    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-05: resetMap
// ---------------------------------------------------------------------------
/**
 * Verifies resetMap():
 *   A. port1 only active → disable then re-enable port1, fetchEntries called
 *   B. both ports active → both disabled, both re-enabled
 *   C. neither port active → verify state via .rm-off existence and no extra api calls
 *   D. api throws → error.value set, .rm-error-wrap rendered
 */
describe('RM-I-05: resetMap', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('scenario A: port1 only active → disable+re-enable port1, fetchEntries called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    vi.mocked(api).mockClear();

    // Click the reset button in the settings panel
    await wrapper.find('.rsp-btn-reset').trigger('click');
    await flushPromises();

    const calls = vi.mocked(api).mock.calls;

    // Verify order: disable port1, re-enable port1, then fetchEntries (cache/json)
    expect(calls[0]).toEqual(['ports/1/mode', { method: 'POST', json: { mode: 'disabled' } }]);
    expect(calls[1]).toEqual(['ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } }]);
    expect(calls[2]).toEqual(['cache/json']);

    // ports/2/mode must NOT be called at all
    const port2Calls = calls.filter((c: unknown[]) => c[0] === 'ports/2/mode');
    expect(port2Calls.length).toBe(0);

    wrapper.unmount();
  });

  it('scenario B: both ports active → both disabled then both re-enabled', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'cache_bus', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    vi.mocked(api).mockClear();

    await wrapper.find('.rsp-btn-reset').trigger('click');
    await flushPromises();

    const calls = vi.mocked(api).mock.calls;

    // Verify order: disable port1, disable port2, enable port1, enable port2, fetchEntries
    expect(calls[0]).toEqual(['ports/1/mode', { method: 'POST', json: { mode: 'disabled' } }]);
    expect(calls[1]).toEqual(['ports/2/mode', { method: 'POST', json: { mode: 'disabled' } }]);
    expect(calls[2]).toEqual(['ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } }]);
    expect(calls[3]).toEqual(['ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } }]);
    expect(calls[4]).toEqual(['cache/json']);

    wrapper.unmount();
  });

  it('scenario C: neither port active → UI hides reset button (resetMap early-return not reachable via UI interaction)', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Both ports disabled → cacheEnabled=false from the start
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    vi.mocked(api).mockClear();

    // Settings panel is hidden when cacheEnabled=false; .rm-off is shown instead.
    // resetMap() would return early if somehow called with both ports disabled in info.value.
    // Verify: the .rm-off element is present (no settings panel / no reset button)
    expect(wrapper.find('.rm-off').exists()).toBe(true);
    expect(wrapper.find('.rsp-btn-reset').exists()).toBe(false);

    // No api calls should have been made after clear (no interaction possible)
    expect(vi.mocked(api)).not.toHaveBeenCalled();

    wrapper.unmount();
  });

  it('scenario D: api throws → error.value set, .rm-error-wrap rendered', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Make the first API call (disable port1) throw
    vi.mocked(api).mockRejectedValueOnce(new Error('Reset failed'));

    await wrapper.find('.rsp-btn-reset').trigger('click');
    await flushPromises();

    // cacheEnabled is still true (infoRef unchanged), loading=false, error is set
    expect(wrapper.find('.rm-error-wrap').exists()).toBe(true);
    expect(wrapper.find('.rm-error-wrap').text()).toContain('Reset failed');

    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-06: saveSettings
// ---------------------------------------------------------------------------
/**
 * Verifies saveSettings():
 *   A. TCP port changed → settings POST called with new port value
 *   B. valueTimeout changed → settings POST called with new timeout
 *   C. status machine: idle → saving → saved → idle (after 3s timer)
 *   D. concurrent save is prevented (api called only once)
 *   E. api error → status becomes 'error', auto-resets after timer
 */
describe('RM-I-06: saveSettings', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    vi.useFakeTimers();
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('scenario A: TCP port changed → settings POST called with cache_modbus_port', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    // Advance just enough for the initial fetch promises to resolve without triggering infinite intervals
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Change the TCP port input value
    const tcpInput = wrapper.find('[data-testid="cache-tcp-port"]');
    await tcpInput.setValue(9999);

    vi.mocked(api).mockClear();

    // Click save and let the async api call resolve
    await wrapper.find('.rsp-btn-save').trigger('click');
    await flushPromises();
    // Allow the 3000ms saveStatusTimer to tick (just clear it, not fully advance)
    vi.advanceTimersByTime(3001);
    await flushPromises();

    // settings POST must have been called with the new TCP port
    expect(vi.mocked(api)).toHaveBeenCalledWith('settings', { method: 'POST', json: { cache_modbus_port: 9999 } });

    // ports api must NOT be called (port modes unchanged — port1 is cache_bus in both info and target)
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    wrapper.unmount();
  });

  it('scenario B: valueTimeout changed → settings POST called with cache_value_timeout_s', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Change the value timeout input
    const timeoutInput = wrapper.find('[data-testid="value-timeout"]');
    await timeoutInput.setValue(30);

    vi.mocked(api).mockClear();

    await wrapper.find('.rsp-btn-save').trigger('click');
    await flushPromises();
    vi.advanceTimersByTime(3001);
    await flushPromises();

    // settings POST must have been called with the new timeout
    expect(vi.mocked(api)).toHaveBeenCalledWith('settings', { method: 'POST', json: { cache_value_timeout_s: 30 } });

    wrapper.unmount();
  });

  it('scenario C: save status machine idle → saving → saved → idle after 3s', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Change TCP port to force a real api call (needed to reliably observe the 'saving' state)
    await wrapper.find('[data-testid="cache-tcp-port"]').setValue(9876);

    const saveBtn = wrapper.find('.rsp-btn-save');

    // Verify initial state: button not disabled
    expect(saveBtn.attributes('disabled')).toBeUndefined();

    // Make the api HANG so the 'saving' state persists long enough to assert
    let resolveSettings!: (v: unknown) => void;
    vi.mocked(api).mockImplementationOnce((url: string) => {
      if (url === 'settings') {
        return new Promise((res) => {
          resolveSettings = res;
        });
      }
      return Promise.resolve({ d: [] });
    });

    // Click save — saveSettings() sets status='saving' synchronously, then awaits api
    saveBtn.trigger('click');
    await wrapper.vm.$nextTick();

    // Status is 'saving' (api is still hanging) — button must be disabled
    expect(saveBtn.attributes('disabled')).toBeDefined();

    // Resolve the api call — status transitions to 'saved'
    resolveSettings({ d: [] });
    await flushPromises();

    // After save completes, button is enabled (status='saved', not 'saving')
    expect(saveBtn.attributes('disabled')).toBeUndefined();

    // Advance timer past 3000ms → status resets to 'idle'
    vi.advanceTimersByTime(3001);
    await flushPromises();

    // Status is 'idle' — button remains enabled
    expect(saveBtn.attributes('disabled')).toBeUndefined();

    wrapper.unmount();
  });

  it('scenario D: concurrent save prevented — settings api called only once despite two clicks', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Change TCP port to force an api call on save
    await wrapper.find('[data-testid="cache-tcp-port"]').setValue(8080);

    // Make api hang (slow promise) to simulate an in-flight request that blocks the save guard
    let resolveApi!: (v: unknown) => void;
    vi.mocked(api).mockImplementationOnce(
      () => new Promise((res) => {
        resolveApi = res;
      }),
    );

    vi.mocked(api).mockClear();
    const saveBtn = wrapper.find('.rsp-btn-save');

    // First click — starts saving (settingsSaveStatus → 'saving')
    saveBtn.trigger('click');
    await flushPromises();

    // Second click while saving is in progress — guard returns early, no extra api call
    saveBtn.trigger('click');
    await flushPromises();

    // Resolve the hanging api call — save completes
    resolveApi({ d: [] });
    await flushPromises();

    // Count only settings-related calls (exclude poll/stats calls triggered by timer advance)
    const settingsCalls = vi.mocked(api).mock.calls.filter(
      (c: unknown[]) => c[0] === 'settings',
    );
    // The settings call must have been made exactly once (second click was a no-op)
    expect(settingsCalls.length).toBe(1);

    wrapper.unmount();
  });

  it('scenario E: api error → status becomes error, auto-resets after 3s', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Change TCP port to ensure a save API call is made
    await wrapper.find('[data-testid="cache-tcp-port"]').setValue(7777);

    // Make api throw to trigger error branch
    vi.mocked(api).mockRejectedValueOnce(new Error('Save failed'));

    await wrapper.find('.rsp-btn-save').trigger('click');
    await flushPromises();

    // Status must be 'error' → .rsp-status shows the error text (i18n key returned as-is)
    expect(wrapper.find('.rsp-status').text()).not.toBe('');

    // After error, save button should not be in 'saving' state (disabled)
    const saveBtn = wrapper.find('.rsp-btn-save');
    expect(saveBtn.attributes('disabled')).toBeUndefined();

    // Advance time past 3000ms — status resets to 'idle' and .rsp-status clears
    vi.advanceTimersByTime(3001);
    await flushPromises();

    // Status is now 'idle' — .rsp-status is empty again
    expect(wrapper.find('.rsp-status').text()).toBe('');

    // Button still enabled after reset
    expect(saveBtn.attributes('disabled')).toBeUndefined();

    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-07: downloadJsonExport
// ---------------------------------------------------------------------------
/**
 * Verifies that clicking the JSON export button:
 *   - calls URL.createObjectURL with a Blob
 *   - creates an anchor element with a matching download filename
 *   - calls URL.revokeObjectURL after the click
 */
describe('RM-I-07: downloadJsonExport', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('creates a blob URL, triggers download with correct filename, revokes URL', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Make api return entries so rawEntries is populated after fetchEntries
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        return { d: [{ s: 1, t: 'h', a: 10, v: 0x1234, age: 5 }] } as never;
      }
      return { d: [] } as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Stub URL global methods
    const createObjectURLMock = vi.fn(() => 'blob:test-url');
    const revokeObjectURLMock = vi.fn();
    vi.stubGlobal('URL', {
      createObjectURL: createObjectURLMock,
      revokeObjectURL: revokeObjectURLMock,
    });

    // Capture the anchor element created via document.createElement
    const appendChildSpy = vi.spyOn(document.body, 'appendChild').mockImplementation((node) => node);
    const clickSpy = vi.fn();
    const originalCreateElement = document.createElement.bind(document);
    // Save spy reference so we can restore it after the test (prevents spy leaking into RM-I-08+)
    const createElementSpy = vi.spyOn(document, 'createElement').mockImplementation((tag: string) => {
      const el = originalCreateElement(tag);
      if (tag === 'a') {
        el.click = clickSpy;
      }
      return el;
    });

    // Click the JSON export button (second .rsp-btn-export)
    const exportButtons = wrapper.findAll('.rsp-btn-export');
    expect(exportButtons.length).toBeGreaterThanOrEqual(2);
    await exportButtons[1].trigger('click');

    // createObjectURL must have been called with a Blob
    expect(createObjectURLMock).toHaveBeenCalledTimes(1);
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const blobArg = (createObjectURLMock.mock.calls as any[][])[0][0];
    expect(blobArg).toBeInstanceOf(Blob);

    // Anchor element was appended (download triggered)
    expect(appendChildSpy).toHaveBeenCalled();
    const anchorEl = appendChildSpy.mock.calls[0][0] as HTMLAnchorElement;
    expect(anchorEl.download).toMatch(/^register-map-\d{4}-\d{2}-\d{2}T\d{2}-\d{2}-\d{2}\.json$/);

    // Anchor must have been clicked to trigger the download
    expect(clickSpy).toHaveBeenCalledTimes(1);

    // revokeObjectURL must have been called after download
    expect(revokeObjectURLMock).toHaveBeenCalledWith('blob:test-url');

    // Restore spies to prevent them from leaking into subsequent tests
    createElementSpy.mockRestore();
    appendChildSpy.mockRestore();
    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-08: expandAll / collapseAll / toggleDevice / toggleGroup
// ---------------------------------------------------------------------------
/**
 * Verifies tree expansion/collapse controls:
 *   - expandAll: all devices become aria-expanded=true
 *   - collapseAll: all nodes close
 *   - toggleDevice: click device row opens it; click again closes it
 *   - toggleGroup: expand device first, click group row opens it; click again closes it
 */
describe('RM-I-08: expandAll / collapseAll / toggleDevice / toggleGroup', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('expandAll opens all device rows; collapseAll closes them', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Return 2 devices with holding registers
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        return {
          d: [
            { s: 1, t: 'h', a: 10, v: 1, age: 5 },
            { s: 1, t: 'i', a: 20, v: 2, age: 3 },
            { s: 2, t: 'h', a: 10, v: 3, age: 8 },
          ],
        } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Verify device rows exist
    const devRows = wrapper.findAll('.rm-row.lvl-dev');
    expect(devRows.length).toBeGreaterThanOrEqual(2);

    // Initially all collapsed (aria-expanded=false)
    for (const row of devRows) {
      expect(row.attributes('aria-expanded')).toBe('false');
    }

    // Click expandAll (first .rm-tb-btn)
    const tbBtns = wrapper.findAll('.rm-tb-btn');
    await tbBtns[0].trigger('click');
    await flushPromises();

    // All device rows should now be expanded
    const expandedRows = wrapper.findAll('.rm-row.lvl-dev');
    for (const row of expandedRows) {
      expect(row.attributes('aria-expanded')).toBe('true');
    }

    // Click collapseAll (second .rm-tb-btn)
    await tbBtns[1].trigger('click');
    await flushPromises();

    // All device rows should be collapsed again
    const collapsedRows = wrapper.findAll('.rm-row.lvl-dev');
    for (const row of collapsedRows) {
      expect(row.attributes('aria-expanded')).toBe('false');
    }

    wrapper.unmount();
  });

  it('toggleDevice: click opens device; click again closes it', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        return { d: [{ s: 1, t: 'h', a: 10, v: 1, age: 5 }] } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const devRow = wrapper.find('.rm-row.lvl-dev');
    expect(devRow.attributes('aria-expanded')).toBe('false');

    // First click → device opens
    await devRow.trigger('click');
    await flushPromises();
    expect(wrapper.find('.rm-row.lvl-dev').attributes('aria-expanded')).toBe('true');

    // Second click → device closes
    await wrapper.find('.rm-row.lvl-dev').trigger('click');
    await flushPromises();
    expect(wrapper.find('.rm-row.lvl-dev').attributes('aria-expanded')).toBe('false');

    wrapper.unmount();
  });

  it('toggleGroup: expand device, click group opens it; click again closes it', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        return {
          d: [
            { s: 1, t: 'h', a: 10, v: 1, age: 5 },
            { s: 1, t: 'h', a: 11, v: 2, age: 3 },
          ],
        } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Expand the device first to reveal group rows
    await wrapper.find('.rm-row.lvl-dev').trigger('click');
    await flushPromises();

    // Group row should now be visible
    const grpRow = wrapper.find('.rm-row.lvl-grp');
    expect(grpRow.exists()).toBe(true);
    expect(grpRow.attributes('aria-expanded')).toBe('false');

    // Click group → opens
    await grpRow.trigger('click');
    await flushPromises();
    expect(wrapper.find('.rm-row.lvl-grp').attributes('aria-expanded')).toBe('true');

    // Click group again → closes
    await wrapper.find('.rm-row.lvl-grp').trigger('click');
    await flushPromises();
    expect(wrapper.find('.rm-row.lvl-grp').attributes('aria-expanded')).toBe('false');

    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-09: cacheEnabled watcher — stats side-effect
// ---------------------------------------------------------------------------
/**
 * Verifies the cacheEnabled watcher side-effects:
 *   A. When cacheEnabled=true on mount, api('cache/status') is called
 *   B. When cacheEnabled transitions from true → false, stats panel hides
 *   C. When cacheEnabled stays false from start, no cache/status call is made
 */
describe('RM-I-09: cacheEnabled watcher — stats side-effect', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('scenario A: cacheEnabled=true on mount → api(cache/status) called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Mock api to return status data for cache/status
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/status') {
        return {
          enabled: true,
          packets_processed: 42,
          entries: 10,
          slaves: 3,
          last_packet_age_us: 1000,
          map_age_us: 2000,
          memory_bytes: 80,
          max_entries: 100,
        } as never;
      }
      return { d: [] } as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // cache/status must have been called (watcher immediate:true fires with cacheEnabled=true)
    expect(vi.mocked(api)).toHaveBeenCalledWith('cache/status');

    wrapper.unmount();
  });

  it('scenario B: cacheEnabled transitions from true → false → stats panel hidden', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/status') {
        return {
          enabled: true,
          packets_processed: 5,
          entries: 2,
          slaves: 1,
          last_packet_age_us: 500,
          map_age_us: 1000,
          memory_bytes: 40,
          max_entries: 50,
        } as never;
      }
      return { d: [] } as never;
    });

    // Start with cacheEnabled=true
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Transition to cacheEnabled=false by disabling both ports
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
    await flushPromises();

    // Stats panel is hidden when cacheEnabled=false → .rm-off is shown
    expect(wrapper.find('.rm-off').exists()).toBe(true);
    // Stats strip is NOT rendered (it's inside v-else branch)
    expect(wrapper.find('.rm-stats').exists()).toBe(false);

    wrapper.unmount();
  });

  it('scenario C: stays false from start → cache/status never called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // infoRef stays undefined → cacheEnabled=false from start
    // Watcher fires with val=false, oldVal=undefined → no stats reset, no fetchCacheStats

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // No crash — component renders the disabled state
    expect(wrapper.find('.rm-off').exists()).toBe(true);
    // cache/status must NOT have been called (cacheEnabled was never true)
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('cache/status');

    wrapper.unmount();
  });
});

// ---------------------------------------------------------------------------
// RM-I-10: onUnmounted — interval/timer cleanup
// ---------------------------------------------------------------------------
/**
 * Verifies that onUnmounted clears all intervals and timers:
 *   A. Unmounting stops the pollInterval and statsInterval (no api calls after unmount)
 *   B. Unmounting stops saveStatusTimer (no error after timer would have fired)
 */
describe('RM-I-10: onUnmounted — interval/timer cleanup', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    vi.useFakeTimers();
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('scenario A: unmount stops pollInterval — no api calls after unmount', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    // Advance 50ms to let the initial fetch settle (intervals fire at 2000ms, 5000ms — not yet)
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Unmount first to ensure intervals are cleared BEFORE advancing time
    wrapper.unmount();

    // Record call count immediately after unmount (before advancing timers)
    const callCountAfterUnmount = vi.mocked(api).mock.calls.length;
    expect(callCountAfterUnmount).toBeGreaterThan(0); // some calls happened during mount

    // Advance 10 seconds — intervals would fire if not cleared by onUnmounted
    vi.advanceTimersByTime(10000);
    await flushPromises();

    // No new api calls should have been made (intervals were cleared on unmount)
    expect(vi.mocked(api).mock.calls.length).toBe(callCountAfterUnmount);
  });

  it('scenario B: unmount stops saveStatusTimer — no state mutations after unmount', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Change TCP port to force a save API call
    await wrapper.find('[data-testid="cache-tcp-port"]').setValue(9876);

    // Start save → saveStatusTimer = setTimeout(..., 3000) is set
    wrapper.find('.rsp-btn-save').trigger('click');
    await flushPromises();

    // Unmount before 3000ms timer fires
    wrapper.unmount();

    // Record api call count immediately after unmount
    const callCountAfterUnmount = vi.mocked(api).mock.calls.length;

    // Advance past 3000ms — if pollInterval wasn't cleared, api would be called again
    vi.advanceTimersByTime(5000);
    await flushPromises();

    // No new api calls after unmount (all intervals and timers cleared by onUnmounted)
    expect(vi.mocked(api).mock.calls.length).toBe(callCountAfterUnmount);
  });
});

// ---------------------------------------------------------------------------
// RM-I-11: fetchEntries — error state
// ---------------------------------------------------------------------------
/**
 * Verifies fetchEntries error handling:
 *   A. api throws on initial fetch → .rm-error-wrap shown with error message
 *   B. recovery after error → subsequent successful fetch clears .rm-error-wrap
 */
describe('RM-I-11: fetchEntries — error state', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    vi.useFakeTimers();
    infoRef.value = undefined;
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('scenario A: api throws on first fetch → .rm-error-wrap shows error message', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    // Make the cache/json fetch throw on every call
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        throw new Error('Network timeout');
      }
      return {} as never;
    });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Error state: .rm-error-wrap should be visible with the error message
    expect(wrapper.find('.rm-error-wrap').exists()).toBe(true);
    expect(wrapper.find('.rm-error-wrap').text()).toContain('Network timeout');

    wrapper.unmount();
  });

  it('scenario B: recovery after error → error cleared on next successful fetch', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // vi.useFakeTimers() is now called in beforeEach; vi.useRealTimers() in afterEach.
    // This ensures fake timers are always restored even if the test fails mid-assertion.

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    // First cache/json call throws; subsequent calls succeed
    let firstCacheFetch = true;
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        if (firstCacheFetch) {
          firstCacheFetch = false;
          throw new Error('Temporary error');
        }
        return { d: [] } as never;
      }
      return {} as never;
    });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    // Let initial fetch settle: advance a small amount (doesn't trigger poll interval)
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // After first fetch, error is shown
    expect(wrapper.find('.rm-error-wrap').exists()).toBe(true);

    // Advance time past the 2000ms poll interval to trigger fetchEntries again
    await vi.advanceTimersByTimeAsync(2001);
    await flushPromises();

    // Error should now be cleared (second fetch succeeded)
    expect(wrapper.find('.rm-error-wrap').exists()).toBe(false);

    wrapper.unmount();
  });
});
