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

// Shared reactive info ref — mutated per test scenario to simulate polls.
const infoRef = ref<Info | undefined>(undefined);

// Mocks — hoisted before any component import by Vitest's vi.mock hoisting.

const fetchInfoMock = vi.fn().mockResolvedValue(undefined);

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: fetchInfoMock }),
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

/**
 * Build a minimal Info object — only the fields consumed by the watch are set.
 *
 * The orthogonal backend model splits the old per-port "mode" into a transport
 * mode (`port_mode`) plus an independent cache overlay flag (`cache_enabled`).
 * For backwards compatibility with existing scenarios, the legacy 'cache_bus'
 * pseudo-mode is translated to the new model: the cache overlay is enabled and
 * the transport is opened as 'passive' (serial open, no TCP). 'disabled' maps to
 * the disabled transport with no cache overlay.
 */
function makeInfo(opts: {
  port1Mode: 'cache_bus' | 'disabled';
  port2Mode: 'cache_bus' | 'disabled';
  tcpPort: number;
  timeout: number;
  tcpServerEnabled?: boolean;
}): Info {
  const port1Transport = opts.port1Mode === 'cache_bus' ? 'passive' : 'disabled';
  const port2Transport = opts.port2Mode === 'cache_bus' ? 'passive' : 'disabled';
  const port1Cache = opts.port1Mode === 'cache_bus';
  const port2Cache = opts.port2Mode === 'cache_bus';
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
      port_mode: port1Transport,
      cache_enabled: port1Cache,
    },
    rs485_2: {
      is_busy: false,
      error_percentage: 0,
      server_connections_count: 0,
      port_mode: port2Transport,
      cache_enabled: port2Cache,
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

describe('RM-I-001: RegisterMap port-initialization guard', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    // Reset info and module registry so each test gets a fresh portsInitialized = false.
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
  });

  afterEach(() => {
    // Restore real timers after each test — scenario 4 uses vi.useFakeTimers().
    vi.useRealTimers();
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

  it('scenario 4: both ports disabled on first poll — port 1 icon defaults to active', async () => {
    // Use fake timers so we can advance the 2000 ms pollInterval manually.
    // This is needed to get loading=false after cacheEnabled becomes true in Phase 2.
    vi.useFakeTimers();
    try {
      const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

      // Phase 1: both ports report 'disabled' (factory-reset state).
      // The watch({ immediate: true }) in RegisterMap.vue fires with (F,F) and calls
      // resolvePortSelection(false, false) → (true, false), setting listenPort1=true.
      // portsInitialized is then set to true, locking subsequent poll updates out.
      //
      // This is the fix under test (RegisterMap.vue lines 185-188).  Without it,
      // listenPort1 would stay false after this first poll.
      infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

      const wrapper = mount(RegisterMap, {
        global: { plugins: [i18n, makeRouter()] },
      });
      // Let the initial onMounted fetchEntries() settle (cacheEnabled=false → it returns
      // early without setting loading=false; that is expected at this phase).
      await vi.advanceTimersByTimeAsync(50);
      await flushPromises();

      // Phase 2: simulate the second info poll where port 1 is now 'cache_bus'.
      // Because portsInitialized=true the watch does NOT overwrite listenPort1 —
      // it keeps the value normalised in Phase 1 (true).
      // cacheEnabled is a computed derived from info, so it becomes true here.
      infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
      await flushPromises();

      // Advance past the 2000 ms pollInterval so fetchEntries() is called while
      // cacheEnabled=true.  fetchEntries() resolves immediately (api mock returns {d:[]})
      // and sets loading=false, making the settings panel (v-else branch) render.
      await vi.advanceTimersByTimeAsync(2000);
      await flushPromises();

      // Direct proof of the fix: portActive(1) reads the .rsp-port-tag button's
      // 'active' CSS class, which reflects listenPort1.  It is true only if
      // resolvePortSelection normalised (F,F)→(T,F) in Phase 1.
      // Without the fix listenPort1 would be false here → this assertion would fail.
      expect(portActive(wrapper, 1)).toBe(true);
      // Port 2 must not be active — the (F,F)→(T,F) normalisation picks port 1 only.
      expect(portActive(wrapper, 2)).toBe(false);

      wrapper.unmount();
    } finally {
      vi.useRealTimers();
    }
  });
});

/**
 * Integration test RM-I-003: RegisterMap toggleCaching — enable path.
 *
 * Verifies that clicking the "Enable caching" button (visible when cacheEnabled=false)
 * enables the cache overlay via api('ports/N/cache', { method: 'POST', json: { enabled: true } })
 * for exactly the ports selected in the Settings panel. Because the ports start as
 * 'disabled', the transport is first opened via api('ports/N/mode', { json: { mode: 'passive' } }).
 * After enabling, fetchEntries (api('cache/json')) is triggered.
 *
 * Three scenarios:
 *   A. listenPort1=true,  listenPort2=false → only ports/1 (passive + cache) + cache/json
 *   B. listenPort1=false, listenPort2=true  → only ports/2 (passive + cache) + cache/json
 *   C. listenPort1=false, listenPort2=false → ports/1 (default, passive + cache) + cache/json
 *
 * Setup strategy:
 *   - infoRef is set BEFORE mount so the immediate watch initialises listenPort1/listenPort2.
 *   - After mount + flushPromises the portsInitialized guard is set, so subsequent
 *     infoRef changes no longer touch the listen-port refs.
 *   - infoRef is then mutated to have both ports 'disabled' so cacheEnabled becomes false,
 *     which renders the "Enable caching" button and puts toggleCaching() on the enable path.
 *   - The api mock is reconfigured so that the ports/N/cache POST call also updates infoRef
 *     to reflect cache_enabled=true, making cacheEnabled true before fetchEntries() runs,
 *     which allows api('cache/json') to be invoked.
 *   - api mock calls are cleared before the button click to isolate assertions.
 */
describe('RM-I-003: toggleCaching enable', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
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

    // Reconfigure the api mock: when ports/1/cache is POSTed, update infoRef to reflect
    // cache_enabled=true so that cacheEnabled becomes true before fetchEntries() runs.
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/1/cache') {
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

    // ports/1/mode must first open the transport as 'passive' (port was disabled)
    expect(apiMock).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'passive' } });

    // ports/1/cache must enable the cache overlay
    expect(apiMock).toHaveBeenCalledWith('ports/1/cache', { method: 'POST', json: { enabled: true } });

    // ports/2 must NOT be touched
    expect(apiMock).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());
    expect(apiMock).not.toHaveBeenCalledWith('ports/2/cache', expect.anything());

    // cache/json must be called (fetchEntries)
    expect(apiMock).toHaveBeenCalledWith('cache/json');

    // fetchInfo must be called to refresh sidebar after toggle
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

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

    // Reconfigure mock: ports/2/cache POST updates infoRef so cacheEnabled turns true.
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/2/cache') {
        infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'cache_bus', tcpPort: 504, timeout: 60 });
      }
      return { d: [] } as never;
    });

    // mockClear() clears call history only; the mockImplementation above is preserved.
    vi.mocked(api).mockClear();

    await wrapper.find('.rm-off button').trigger('click');
    await flushPromises();

    const apiMock = vi.mocked(api);

    // ports/1 must NOT be touched
    expect(apiMock).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());
    expect(apiMock).not.toHaveBeenCalledWith('ports/1/cache', expect.anything());

    // ports/2/mode must first open the transport as 'passive' (port was disabled)
    expect(apiMock).toHaveBeenCalledWith('ports/2/mode', { method: 'POST', json: { mode: 'passive' } });

    // ports/2/cache must enable the cache overlay
    expect(apiMock).toHaveBeenCalledWith('ports/2/cache', { method: 'POST', json: { enabled: true } });

    // cache/json must be called (fetchEntries)
    expect(apiMock).toHaveBeenCalledWith('cache/json');

    // fetchInfo must be called to refresh sidebar after toggle
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });

  it('scenario C: both false (edge case) — defaults to ports/1/mode, ports/2/mode not called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // First poll: both ports disabled → listenPort1=false, listenPort2=false after watch.
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // cacheEnabled is already false; no need to mutate infoRef again.
    // Reconfigure mock: ports/1/cache POST updates infoRef so cacheEnabled turns true
    // (default-port-1 fallback branch inside toggleCaching).
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/1/cache') {
        infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
      }
      return { d: [] } as never;
    });

    // mockClear() clears call history only; the mockImplementation above is preserved.
    vi.mocked(api).mockClear();

    await wrapper.find('.rm-off button').trigger('click');
    await flushPromises();

    const apiMock = vi.mocked(api);

    // ports/1/mode must first open the transport as 'passive' (default fallback, port disabled)
    expect(apiMock).toHaveBeenCalledWith('ports/1/mode', { method: 'POST', json: { mode: 'passive' } });

    // ports/1/cache must enable the cache overlay (default fallback)
    expect(apiMock).toHaveBeenCalledWith('ports/1/cache', { method: 'POST', json: { enabled: true } });

    // ports/2 must NOT be touched
    expect(apiMock).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());
    expect(apiMock).not.toHaveBeenCalledWith('ports/2/cache', expect.anything());

    // cache/json must be called (fetchEntries)
    expect(apiMock).toHaveBeenCalledWith('cache/json');

    // fetchInfo must be called to refresh sidebar after toggle
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });
});

/**
 * Verifies that clicking the caching toggle when cacheEnabled=true (disable path):
 *   A. calls ports/1/cache disabled only (port1 cache on, port2 off)
 *   B. calls both ports/1/cache and ports/2/cache disabled (both cache on)
 *   C. sets error.value on API failure (rendered via .rm-error-wrap)
 * The transport mode is never touched on the disable path.
 */
describe('RM-I-04: toggleCaching — disable path', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('scenario A: only port1 cache on → only ports/1/cache disable called', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Clear call history — isolate only the toggle-click calls
    vi.mocked(api).mockClear();

    // Click the caching toggle label (calls toggleCaching() on the disable path)
    await wrapper.find('.rm-caching-toggle').trigger('click');
    await flushPromises();

    // ports/1/cache must be called with enabled: false
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/cache', { method: 'POST', json: { enabled: false } });

    // ports/2/cache must NOT be called (port2 cache was already off)
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/cache', expect.anything());

    // The transport mode must never be touched on disable
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    // fetchInfo must be called to refresh sidebar after toggle
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });

  it('scenario B: both ports cache on → both ports/1/cache and ports/2/cache disabled', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'cache_bus', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    vi.mocked(api).mockClear();

    await wrapper.find('.rm-caching-toggle').trigger('click');
    await flushPromises();

    // Both ports' cache overlays must be disabled
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/cache', { method: 'POST', json: { enabled: false } });
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/2/cache', { method: 'POST', json: { enabled: false } });

    // The transport mode must never be touched on disable
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());

    // fetchInfo must be called to refresh sidebar after toggle
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

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
    fetchInfoMock.mockClear();
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

    // Verify order: cache off port1, cache on port1, then fetchEntries (cache/json)
    expect(calls[0]).toEqual(['ports/1/cache', { method: 'POST', json: { enabled: false } }]);
    expect(calls[1]).toEqual(['ports/1/cache', { method: 'POST', json: { enabled: true } }]);
    expect(calls[2]).toEqual(['cache/json']);

    // ports/2/cache must NOT be called at all
    const port2Calls = calls.filter((c: unknown[]) => c[0] === 'ports/2/cache');
    expect(port2Calls.length).toBe(0);

    // The transport mode must never be touched on reset
    const modeCalls = calls.filter((c: unknown[]) => c[0] === 'ports/1/mode' || c[0] === 'ports/2/mode');
    expect(modeCalls.length).toBe(0);

    // fetchInfo must be called to refresh sidebar after reset
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

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

    // Verify order: cache off port1, cache off port2, cache on port1, cache on port2, fetchEntries
    expect(calls[0]).toEqual(['ports/1/cache', { method: 'POST', json: { enabled: false } }]);
    expect(calls[1]).toEqual(['ports/2/cache', { method: 'POST', json: { enabled: false } }]);
    expect(calls[2]).toEqual(['ports/1/cache', { method: 'POST', json: { enabled: true } }]);
    expect(calls[3]).toEqual(['ports/2/cache', { method: 'POST', json: { enabled: true } }]);
    expect(calls[4]).toEqual(['cache/json']);

    // fetchInfo must be called to refresh sidebar after reset
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

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

/**
 * Verifies saveSettings():
 *   A. TCP port changed → settings POST called with new port value
 *   B. valueTimeout changed → settings POST called with new timeout
 *   C. status machine: idle → saving → saved → idle (after 3s timer)
 *   D. concurrent save is prevented (api called only once)
 *   E. api error → status becomes 'error', auto-resets after timer
 *   F. per-port cache-overlay branch: enabling on a 'disabled' port posts mode 'passive'
 *      THEN cache enabled; disabling posts only cache disabled (no mode call).
 */
describe('RM-I-06: saveSettings', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    vi.useFakeTimers();
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
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

    // fetchInfo must be called to refresh sidebar after save
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

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

    // fetchInfo must be called to refresh sidebar after save
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });

  it('scenario B2: valueTimeout above max → settings POST clamps to 65535', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Enter a value above the firmware limit (65535 s)
    const timeoutInput = wrapper.find('[data-testid="value-timeout"]');
    await timeoutInput.setValue(70000);

    vi.mocked(api).mockClear();

    await wrapper.find('.rsp-btn-save').trigger('click');
    await flushPromises();
    vi.advanceTimersByTime(3001);
    await flushPromises();

    // POST must carry the clamped value, not the raw 70000
    expect(vi.mocked(api)).toHaveBeenCalledWith('settings', { method: 'POST', json: { cache_value_timeout_s: 65535 } });

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

  it('scenario F: per-port overlay — enable on a disabled port posts mode passive THEN cache; disable posts only cache', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // First poll: port1 = cache_bus (cache ON, transport 'passive'), port2 = disabled
    // (cache OFF, transport 'disabled'). cacheEnabled=true so the settings panel renders and
    // the Save button is reachable. The watch initialises listenPort1=true, listenPort2=false.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Select port 2 (radio): listenPort1 → false, listenPort2 → true. This makes BOTH ports
    // differ from info: port1 (info cache ON → target OFF) and port2 (info cache OFF → target ON).
    const portTags = wrapper.findAll('.rsp-port-tag');
    expect(portTags.length).toBeGreaterThanOrEqual(2);
    await portTags[1].trigger('click'); // click the "2" tag → selectListenPort(2)

    vi.mocked(api).mockClear();

    // Save: applies the per-port cache overlay for both changed ports.
    await wrapper.find('.rsp-btn-save').trigger('click');
    await flushPromises();
    vi.advanceTimersByTime(3001);
    await flushPromises();

    const calls = vi.mocked(api).mock.calls.filter(
      (c: unknown[]) => c[0] === 'ports/1/mode' || c[0] === 'ports/1/cache'
        || c[0] === 'ports/2/mode' || c[0] === 'ports/2/cache',
    );

    // Positive enable branch (port 2 was 'disabled'): mode 'passive' BEFORE cache enabled.
    const port2ModeIdx = calls.findIndex((c) => c[0] === 'ports/2/mode');
    const port2CacheIdx = calls.findIndex((c) => c[0] === 'ports/2/cache');
    expect(port2ModeIdx).toBeGreaterThanOrEqual(0);
    expect(port2CacheIdx).toBeGreaterThanOrEqual(0);
    expect(port2ModeIdx).toBeLessThan(port2CacheIdx); // mode passive POSTed first
    expect(calls[port2ModeIdx]).toEqual(['ports/2/mode', { method: 'POST', json: { mode: 'passive' } }]);
    expect(calls[port2CacheIdx]).toEqual(['ports/2/cache', { method: 'POST', json: { enabled: true } }]);

    // Symmetric disable branch (port 1 cache was ON → target OFF): only cache disabled, NO mode.
    expect(vi.mocked(api)).toHaveBeenCalledWith('ports/1/cache', { method: 'POST', json: { enabled: false } });
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());

    // fetchInfo must refresh the sidebar after save.
    expect(fetchInfoMock).toHaveBeenCalledWith('low');

    wrapper.unmount();
  });
});

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
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('creates a blob URL, triggers download with correct filename, revokes URL', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Make api return entries so rawEntries is populated after fetchEntries.
    // cache/status must report enabled:true — the export buttons are gated on the
    // device-confirmed cache state, not on the optimistic cacheEnabled toggle.
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/status') {
        return { enabled: true } as never;
      }
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
    fetchInfoMock.mockClear();
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
    fetchInfoMock.mockClear();
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
    fetchInfoMock.mockClear();
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
    fetchInfoMock.mockClear();
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

/**
 * Verifies that typing into the search input (.rm-search input) filters the device tree
 * by decimal or hexadecimal slave ID.
 *
 * The filterDevices utility performs: trim+lowercase on the query, then checks
 * d.id.toString().includes(q) || d.id.toString(16).toLowerCase().includes(q).
 *
 * Three entries are used: slave 1 (h), slave 2 (h), slave 10 (h).
 *   - Query "2"  → only slave 2 visible (decimal match)
 *   - Query "a"  → only slave 10 visible (hex match: 10 → "a")
 *   - Empty ""   → all 3 visible again
 */
describe('RM-I-12: searchFilter — filter devices by slave ID', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('filters device rows by decimal and hexadecimal slave ID', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Return 3 slaves so the search filter has something to filter
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        return {
          d: [
            { s: 1, t: 'h', a: 10, v: 1, age: 1 },
            { s: 2, t: 'h', a: 10, v: 2, age: 1 },
            { s: 10, t: 'h', a: 10, v: 10, age: 1 },
          ],
        } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // A. Empty query — all 3 device rows visible
    expect(wrapper.findAll('.rm-row.lvl-dev').length).toBe(3);

    // B. Type "2" — only slave 2 matches decimal "2"; slave 10 does not contain "2"
    await wrapper.find('.rm-search input').setValue('2');
    await flushPromises();
    expect(wrapper.findAll('.rm-row.lvl-dev').length).toBe(1);
    // Verify it is slave 2 (hex: 0x02) that remains, not another node
    expect(wrapper.find('.rm-row.lvl-dev .rm-slave').text()).toBe('Slave 2 (0x02)');

    // C. Type "a" — only slave 10 matches (hex representation is "a")
    await wrapper.find('.rm-search input').setValue('a');
    await flushPromises();
    expect(wrapper.findAll('.rm-row.lvl-dev').length).toBe(1);
    // Verify it is slave 10 (hex: 0x0A) that remains, not another node
    expect(wrapper.find('.rm-row.lvl-dev .rm-slave').text()).toBe('Slave 10 (0x0A)');

    // D. Clear query — all 3 visible again
    await wrapper.find('.rm-search input').setValue('');
    await flushPromises();
    expect(wrapper.findAll('.rm-row.lvl-dev').length).toBe(3);

    wrapper.unmount();
  });
});

/**
 * Verifies that clicking .rsp-port-tag buttons switches the active port selection.
 *
 * Initial state (port1=cache_bus): listenPort1=true (button 1 active), listenPort2=false.
 * Click port 2: port 2 becomes active, port 1 inactive.
 * Click port 1: port 1 becomes active again, port 2 inactive.
 */
describe('RM-I-13: selectListenPort — clicking port-tag buttons', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('clicking port-tag buttons switches the active port selection', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // port1=cache_bus → listenPort1=true, listenPort2=false after init
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // A. Initial state: port 1 active, port 2 inactive
    expect(portActive(wrapper, 1)).toBe(true);
    expect(portActive(wrapper, 2)).toBe(false);

    // B. Click port 2 button → port 2 active, port 1 inactive
    await wrapper.findAll('.rsp-port-tag')[1].trigger('click');
    await flushPromises();
    expect(portActive(wrapper, 2)).toBe(true);
    expect(portActive(wrapper, 1)).toBe(false);

    // C. Click port 1 button → port 1 active again, port 2 inactive
    await wrapper.findAll('.rsp-port-tag')[0].trigger('click');
    await flushPromises();
    expect(portActive(wrapper, 1)).toBe(true);
    expect(portActive(wrapper, 2)).toBe(false);

    wrapper.unmount();
  });
});

/**
 * Verifies that clicking the first .rsp-btn-export (CSV) calls
 * window.open('/cache/csv', '_blank', 'noopener').
 */
describe('RM-I-14: Export CSV button — window.open call', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('clicking CSV export button calls window.open with correct arguments', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Return an entry so the component reaches the enabled/loaded state and shows export buttons.
    // cache/status must report enabled:true — the export buttons are gated on the
    // device-confirmed cache state, not on the optimistic cacheEnabled toggle.
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/status') {
        return { enabled: true } as never;
      }
      if (url === 'cache/json') {
        return { d: [{ s: 1, t: 'h', a: 10, v: 100, age: 2 }] } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Stub window.open before clicking
    const openMock = vi.fn();
    vi.stubGlobal('open', openMock);

    // Click the first .rsp-btn-export (CSV export)
    const exportButtons = wrapper.findAll('.rsp-btn-export');
    expect(exportButtons.length).toBeGreaterThanOrEqual(1);
    await exportButtons[0].trigger('click');

    // Verify window.open was called with the correct URL and options
    expect(openMock).toHaveBeenCalledWith('/cache/csv', '_blank', 'noopener');

    wrapper.unmount();
  });
});

/**
 * Verifies that toggling the TCP serve switch and clicking Save causes
 * api('settings', { method: 'POST', json: { cache_modbus_server_enabled: false } })
 * to be called, while the ports API is not called (port mode unchanged).
 */
describe('RM-I-15: tcpServeEnabled toggle + save', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('toggling TCP serve switch off and saving POSTs cache_modbus_server_enabled=false', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // tcpServerEnabled=true so the switch starts checked
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60, tcpServerEnabled: true });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Verify initial state: TCP serve switch is checked (enabled)
    expect(switchChecked(wrapper, 'rsp-tcp-serve')).toBe(true);

    // Toggle the switch off by setting its value to false
    await wrapper.find('#rsp-tcp-serve').setValue(false);

    // Clear recorded API calls so we only assert on the save call
    vi.mocked(api).mockClear();

    // Click save
    await wrapper.find('.rsp-btn-save').trigger('click');
    await flushPromises();

    // settings POST must be called with cache_modbus_server_enabled: false
    expect(vi.mocked(api)).toHaveBeenCalledWith('settings', {
      method: 'POST',
      json: { cache_modbus_server_enabled: false },
    });

    // Port mode/cache must NOT be changed (cache state matches the listen-port selection)
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/mode', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/1/cache', expect.anything());
    expect(vi.mocked(api)).not.toHaveBeenCalledWith('ports/2/cache', expect.anything());

    wrapper.unmount();
  });
});

/**
 * Verifies that a register row with age >= valueTimeout gets the .stale class
 * and renders .stale-dot, while a fresh row does not.
 *
 * Stale logic from buildRegsByKey (mirrors the firmware age_s >= value_timeout_s):
 *   stale = valueTimeout > 0 && updatedAge >= valueTimeout
 *
 * Setup:
 *   - valueTimeout = 10 s (from makeInfo timeout: 10)
 *   - Entry 1: slave 1, addr 10, age 5  → fresh (5 < 10)
 *   - Entry 2: slave 1, addr 20, age 20 → stale (20 >= 10)
 *
 * Rows are sorted by address ascending: row[0]=addr10 (fresh), row[1]=addr20 (stale).
 */
describe('RM-I-16: Stale indicator — .stale class and .stale-dot', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('stale register row has .stale class and .stale-dot; fresh row does not', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Both entries belong to slave 1, holding registers; different addresses and ages
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/json') {
        return {
          d: [
            { s: 1, t: 'h', a: 10, v: 100, age: 5 }, // fresh: age 5 < timeout 10
            { s: 1, t: 'h', a: 20, v: 200, age: 20 }, // stale: age 20 > timeout 10
          ],
        } as never;
      }
      return {} as never;
    });

    // timeout: 10 → valueTimeout = 10 s
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 10 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Expand the device row to reveal group rows
    await wrapper.find('.rm-row.lvl-dev').trigger('click');
    await flushPromises();

    // Expand the group row (Holding registers) to reveal register rows
    await wrapper.find('.rm-row.lvl-grp').trigger('click');
    await flushPromises();

    // Both register rows should now be visible
    const regRows = wrapper.findAll('.rm-row.lvl-reg');
    expect(regRows.length).toBe(2);

    // Row 0 = addr 10 (fresh) — must NOT have .stale class
    expect(regRows[0].classes()).not.toContain('stale');
    expect(regRows[0].find('.stale-dot').exists()).toBe(false);
    // Verify hex address column renders correctly (addr 10 → "0x000A")
    expect(regRows[0].find('.rm-reg-addr-hex').text()).toBe('0x000A');

    // Row 1 = addr 20 (stale) — must have .stale class and .stale-dot
    expect(regRows[1].classes()).toContain('stale');
    expect(regRows[1].find('.stale-dot').exists()).toBe(true);
    // Verify hex address column for stale row (addr 20 → "0x0014")
    expect(regRows[1].find('.rm-reg-addr-hex').text()).toBe('0x0014');

    wrapper.unmount();
  });
});

/**
 * Verifies that values returned by cache/status are rendered in the stats strip DOM.
 *
 * Stats strip .stat-block layout:
 *   [0] Slaves / Registers — derived from rawEntries, NOT from cache/status
 *   [1] Packets processed  — cachePackets = status.packets_processed
 *   [2] Last packet        — cacheLastPacketAgeUs = status.last_packet_age_us
 *   [3] Map age            — cacheMapAgeUs = status.map_age_us
 *   [4] Memory             — cacheMemoryBytes, cacheMaxEntries, cacheEntries
 *
 * This test asserts:
 *   - stat-block[1] .stat-value text = '42' (packets_processed)
 *   - .stat-entries-val text = '7' (entries)
 */
describe('RM-I-17: Stats DOM values from cache/status', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('renders packets_processed and entries values from cache/status in the stats strip', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Mock api: return status data for cache/status and entries for cache/json
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/status') {
        return {
          enabled: true,
          packets_processed: 42,
          entries: 7,
          slaves: 2,
          last_packet_age_us: 0,
          map_age_us: 0,
          memory_bytes: 0,
          max_entries: 0,
        } as never;
      }
      if (url === 'cache/json') {
        return {
          d: [
            { s: 1, t: 'h', a: 10, v: 1, age: 1 },
            { s: 2, t: 'h', a: 10, v: 2, age: 1 },
          ],
        } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Stats strip must be rendered (cacheEnabled=true, loading=false, no error)
    expect(wrapper.find('.rm-stats').exists()).toBe(true);

    // Find the "packets processed" stat-block by its label text rather than by DOM index,
    // so the assertion stays correct even if the order of stat blocks changes.
    // The component uses SFC-level i18n (en.stat_packets = "Packets processed").
    const statBlocks = wrapper.findAll('.stat-block');
    expect(statBlocks.length).toBeGreaterThanOrEqual(2);
    const packetsBlock = statBlocks.find(
      (b) => b.find('.stat-label').text() === 'Packets processed',
    );
    expect(packetsBlock).toBeDefined();
    expect(packetsBlock!.find('.stat-value').text()).toBe('42');

    // .stat-entries-val displays cacheEntries (entries = 7)
    expect(wrapper.find('.stat-entries-val').text()).toBe('7');

    wrapper.unmount();
  });
});

/**
 * Verifies that isMutating prevents a second toggleCaching() call from starting
 * while the first one is still in-flight.
 *
 * Test: make the ports/1/cache POST hang, double-click the toggle, resolve the
 * hanging promise, and verify that ports/1/cache was called exactly once.
 */
describe('RM-I-18: isMutating guard — toggleCaching double-click prevention', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('double-click on caching toggle issues only one ports/1/cache call', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Start with cacheEnabled=true (port1 cache on) so the toggle is on the disable path.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Make the ports/1/cache POST hang indefinitely to simulate an in-flight request.
    let resolveApiCall!: (v: unknown) => void;
    vi.mocked(api).mockImplementation(
      (url: string) =>
        new Promise((res) => {
          if (url === 'ports/1/cache') {
            resolveApiCall = res;
          } else {
            res({ d: [] });
          }
        }),
    );

    // Clear call history so we only count toggle-click calls.
    vi.mocked(api).mockClear();

    // First click — starts the disable operation (isMutating becomes true).
    await wrapper.find('.rm-caching-toggle').trigger('click');
    await wrapper.vm.$nextTick();

    // Second click while the first is still in-flight — must be ignored.
    await wrapper.find('.rm-caching-toggle').trigger('click');
    await wrapper.vm.$nextTick();

    // Resolve the hanging call so the component can finish.
    resolveApiCall({ d: [] });
    await flushPromises();

    // Count only ports/1/cache calls — must be exactly 1.
    const port1Calls = vi.mocked(api).mock.calls.filter(
      (c: unknown[]) => c[0] === 'ports/1/cache',
    );
    expect(port1Calls.length).toBe(1);

    wrapper.unmount();
  });

  it('double-click on reset button issues only one disable+re-enable cycle', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Start with cacheEnabled=true (port1=cache_bus) so the reset button is visible.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Track how many times ports/1/cache has been called.
    // The first call hangs (so the second button click arrives while the first resetMap is in-flight).
    // All subsequent calls resolve immediately.
    let port1CallCount = 0;
    let resolveFirstPort1Call!: (v: unknown) => void;
    vi.mocked(api).mockImplementation(
      (url: string) =>
        new Promise((res) => {
          if (url === 'ports/1/cache') {
            port1CallCount += 1;
            if (port1CallCount === 1) {
              // Hang the first call so the second click arrives before resetMap() finishes.
              resolveFirstPort1Call = res;
            } else {
              // All subsequent ports/1/cache calls resolve immediately.
              res({ d: [] });
            }
          } else {
            // All non-ports/1/cache calls (cache/json, etc.) resolve immediately.
            res({ d: [] });
          }
        }),
    );

    // Clear call history so we only count reset-click calls.
    vi.mocked(api).mockClear();
    port1CallCount = 0;

    // First click — starts resetMap() (isMutating becomes true).
    await wrapper.find('.rsp-btn-reset').trigger('click');
    await wrapper.vm.$nextTick();

    // Second click while the first resetMap() is still in-flight — must be ignored by the guard.
    await wrapper.find('.rsp-btn-reset').trigger('click');
    await wrapper.vm.$nextTick();

    // Resolve the hanging first ports/1/cache call so the single resetMap() can finish.
    resolveFirstPort1Call({ d: [] });
    await flushPromises();

    // Count only ports/1/cache calls.
    // One resetMap() execution = 2 calls (cache off then cache on).
    // With the isMutating guard: ≤2 calls total.
    // Without the guard (two parallel executions): 4 calls total.
    const port1Calls = vi.mocked(api).mock.calls.filter(
      (c: unknown[]) => c[0] === 'ports/1/cache',
    );
    expect(port1Calls.length).toBe(2);

    wrapper.unmount();
  });
});

/**
 * Verifies that the cacheEnabled watcher clears any existing statsInterval before
 * starting a new one, so a true→false→true transition does not leave two overlapping
 * intervals running simultaneously.
 *
 * With the fix: only one setInterval is active after the second true transition.
 * After 10 001ms, a single 5000ms interval fires at ~5000ms and ~10000ms → 2 calls.
 *
 * Without the fix: two intervals overlap → ~4 calls in 10 001ms.
 */
describe('RM-I-19: statsInterval not double-scheduled on cacheEnabled flicker', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    vi.useFakeTimers();
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.useRealTimers();
  });

  it('cache/status called at most 2 times in 10s after a true→false→true flicker', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // Step 1: Start with cacheEnabled=true → watcher fires, first interval starts.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    // Settle the initial mount without advancing timers into the intervals.
    await vi.advanceTimersByTimeAsync(50);
    await flushPromises();

    // Step 2: cacheEnabled → false (info undefined simulates a brief poll gap).
    // The watcher's else branch clears statsInterval.
    infoRef.value = undefined;
    await wrapper.vm.$nextTick();
    await flushPromises();

    // Step 3: cacheEnabled → true again — the watcher must clear any stale interval
    // before starting a new one, then calls fetchCacheStats immediately.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
    await wrapper.vm.$nextTick();
    await flushPromises();

    // Clear all recorded calls so we count only from step 3 onwards.
    vi.mocked(api).mockClear();

    // Step 4: Advance 10 001ms — a single 5000ms interval fires at ~5000ms and ~10000ms.
    // The immediate fetchCacheStats at step 3 was already counted and cleared above.
    await vi.advanceTimersByTimeAsync(10001);
    await flushPromises();

    // Count cache/status calls in this window.
    const statusCalls = vi.mocked(api).mock.calls.filter(
      (c: unknown[]) => c[0] === 'cache/status',
    );

    // With the fix: exactly 2 calls (one at ~5000ms, one at ~10000ms).
    // Without the fix: 4+ calls (two overlapping intervals each firing twice).
    expect(statusCalls.length).toBe(2);

    wrapper.unmount();
  });
});

/**
 * RM-I-20: the CSV/JSON export buttons are gated on the DEVICE-CONFIRMED cache state
 * (GET /cache/status → `enabled`), not on the optimistic `cacheEnabled` toggle.
 *
 * cacheEnabled is derived from the per-port overlay flags and can read true before the
 * device has confirmed anything (useOptimisticToggle). GET /cache/csv answers 409 while
 * the cache pool is off, so a button armed on optimism alone opens a tab reading
 * "cache disabled" — and the JSON export would write out an empty file.
 */
describe('RM-I-20: export buttons gated on the device-confirmed cache state', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  afterEach(() => {
    vi.unstubAllGlobals();
  });

  it('disables both export buttons while cache/status reports enabled:false', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // The per-port overlay flags say "on" (so cacheEnabled — the optimistic value — is
    // true and the panel renders), but the device reports the cache pool as OFF.
    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/status') {
        return { enabled: false } as never;
      }
      if (url === 'cache/json') {
        return { d: [{ s: 1, t: 'h', a: 10, v: 100, age: 2 }] } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const exportButtons = wrapper.findAll('.rsp-btn-export');
    expect(exportButtons.length).toBe(2);
    for (const btn of exportButtons) {
      expect(btn.attributes('disabled')).toBeDefined();
      // A hint must explain why the button is dead.
      expect(btn.attributes('title')).toBeTruthy();
    }

    // Clicking a disabled CSV button must not open a tab on the 409.
    const openMock = vi.fn();
    vi.stubGlobal('open', openMock);
    await exportButtons[0].trigger('click');
    expect(openMock).not.toHaveBeenCalled();

    wrapper.unmount();
  });

  it('enables both export buttons once cache/status reports enabled:true', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'cache/status') {
        return { enabled: true } as never;
      }
      if (url === 'cache/json') {
        return { d: [{ s: 1, t: 'h', a: 10, v: 100, age: 2 }] } as never;
      }
      return {} as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    const exportButtons = wrapper.findAll('.rsp-btn-export');
    expect(exportButtons.length).toBe(2);
    for (const btn of exportButtons) {
      expect(btn.attributes('disabled')).toBeUndefined();
    }

    wrapper.unmount();
  });
});

/**
 * RM-I-21: enabling the cache must arm the export buttons as soon as the toggle lands,
 * not up to 5 s later.
 *
 * cacheActive (the device-confirmed flag the export buttons are gated on) is only ever
 * written by fetchCacheStats(). The watch on cacheEnabled fires one fetchCacheStats() the
 * instant useOptimisticToggle flips the override to true — but that GET /cache/status is NOT
 * serialised with the POST /ports/{n}/cache that is still in flight. When the GET wins the
 * race the device honestly answers enabled:false, cacheActive stays false, and nothing
 * re-fetches: the watch will not fire again (cacheEnabled did not change), so the buttons
 * stay greyed out until the next 5 s stats-interval tick.
 *
 * This test pins the losing order explicitly: the status GET is served while the cache POST
 * is still pending. toggleCaching() must re-fetch the stats after the toggle completes.
 */
describe('RM-I-21: export buttons arm immediately after the cache toggle lands', () => {
  const i18n = createI18n({ legacy: false, locale: 'en', messages: { en: {} } });

  beforeEach(() => {
    infoRef.value = undefined;
    fetchInfoMock.mockClear();
    vi.resetModules();
    vi.mocked(api).mockReset();
    vi.mocked(api).mockResolvedValue({ d: [] } as never);
  });

  it('re-fetches cache/status after the enable POST resolves, without waiting for the interval', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    // The device's own view of the cache pool — what GET /cache/status reports.
    let deviceCacheOn = false;
    // Holds the enable POST open so the status GET provoked by the optimistic flip is
    // guaranteed to be served first — the exact interleaving that used to strand the buttons.
    let releasePost: () => void = () => {};
    const postGate = new Promise<void>((resolve) => {
      releasePost = resolve;
    });

    // First poll: port 1 has the cache overlay on, so listenPort1 initialises to true and
    // the initial fetchEntries() settles `loading`.
    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    // Now the cache is off on both ports (portsInitialized keeps listenPort1 = true).
    infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
    await flushPromises();

    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/1/cache') {
        await postGate; // the device has not applied anything yet
        deviceCacheOn = true;
        infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
        return {} as never;
      }
      if (url === 'cache/status') {
        return { enabled: deviceCacheOn } as never;
      }
      if (url === 'cache/json') {
        return { d: [{ s: 1, t: 'h', a: 10, v: 100, age: 2 }] } as never;
      }
      return {} as never;
    });
    vi.mocked(api).mockClear();

    // Click "Enable caching". The optimistic override flips cacheEnabled to true at once, so
    // the watch fires a status GET while the POST above is still parked on postGate.
    await wrapper.find('.rm-off button').trigger('click');
    await flushPromises();

    // The racing GET has been served and truthfully reported the cache as still off.
    expect(vi.mocked(api)).toHaveBeenCalledWith('cache/status');
    expect(deviceCacheOn).toBe(false);

    // Let the device apply the change and the toggle finish.
    releasePost();
    await flushPromises();

    // No timers advanced: the only thing that can have refreshed cacheActive is the fetch
    // toggleCaching() runs after cacheToggle.run() settles. The export buttons must be live.
    const exportButtons = wrapper.findAll('.rsp-btn-export');
    expect(exportButtons.length).toBe(2);
    for (const btn of exportButtons) {
      expect(btn.attributes('disabled')).toBeUndefined();
    }

    wrapper.unmount();
  });

  it('disabling the cache issues no extra cache/status request', async () => {
    const { default: RegisterMap } = await import('@/views/RegisterMap.vue');

    vi.mocked(api).mockImplementation(async (url: string) => {
      if (url === 'ports/1/cache') {
        infoRef.value = makeInfo({ port1Mode: 'disabled', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });
        return {} as never;
      }
      if (url === 'cache/status') {
        return { enabled: true } as never;
      }
      return { d: [] } as never;
    });

    infoRef.value = makeInfo({ port1Mode: 'cache_bus', port2Mode: 'disabled', tcpPort: 504, timeout: 60 });

    const wrapper = mount(RegisterMap, { global: { plugins: [i18n, makeRouter()] } });
    await flushPromises();

    vi.mocked(api).mockClear();

    // Turn the cache off via the header toggle.
    await wrapper.find('.rm-caching-toggle').trigger('click');
    await flushPromises();

    // The trailing fetchCacheStats() must be a no-op on this path: it is gated on
    // cacheEnabled, which the optimistic override has already driven to false. Polling the
    // status of a cache we just switched off would be a wasted round-trip.
    const statusCalls = vi.mocked(api).mock.calls.filter((c: unknown[]) => c[0] === 'cache/status');
    expect(statusCalls.length).toBe(0);

    wrapper.unmount();
  });
});
