/**
 * Integration test RM-I-001: RegisterMap settings panel — port initialization guard.
 *
 * Verifies that the `portsInitialized` guard inside RegisterMap.vue prevents
 * subsequent info poll updates from overwriting user-editable fields after the
 * first load.
 *
 * Three scenarios (split across two `it` blocks, each with a fresh module instance):
 *   1. First poll initialises cacheTcpPort, valueTimeout, listenPort1, listenPort2 from info.
 *   2. Second poll (different values) does NOT overwrite those fields.
 *   3. Double-true guard: if both rs485 ports report 'cache_bus' on first poll,
 *      listenPort2 is forced to false.
 *
 * Rendering notes:
 *   The settings panel (which contains the inputs under test) is gated by:
 *     v-if="!cacheEnabled"    → caching-disabled placeholder
 *     v-else-if="loading"     → spinner (loading starts true)
 *     v-else                  → settings panel (shown when cacheEnabled && !loading)
 *   `loading` is set to false inside fetchEntries() after the api() promise resolves.
 *   Therefore we must flushPromises() after mount to let the async onMounted settle
 *   before inspecting the DOM.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory } from 'vue-router';
import type { Info } from '@/common/types';

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
