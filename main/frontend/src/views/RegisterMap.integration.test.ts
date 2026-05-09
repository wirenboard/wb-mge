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

import { describe, it, expect, vi, beforeEach } from 'vitest';
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
