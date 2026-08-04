/**
 * Integration tests for the firmware line on Dashboard.vue. This is the only place in the UI that
 * names the version offered in the selected channel — the System card deliberately does not repeat
 * it — so the sentence is only reachable from here. The channel-release composable is the real one;
 * fw-releases (fetch) and the device (api) are stubbed.
 *
 * DASH-I-001 — an update is available in the selected channel: the line names the channel and the
 *               offered version, the version comes from the manifest (no latest.txt request), and
 *               the button over to /system is offered.
 * DASH-I-002 — the device already runs the channel version: "up to date", no offer, no button.
 * DASH-I-003 — the check could not be performed at all: "update check unavailable", no offer, no
 *               button.
 * DASH-I-004 — the manifest has no block for this board: its own sentence, again no offer.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref, shallowRef } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory, matchedRouteKey } from 'vue-router';
import { messages } from '@/i18n/messages';
import manifest from '@/common/__fixtures__/release-versions.sample.yaml?raw';
import { api } from '@/utils/api';
import type { Info, Settings } from '@/common/types';

// Enough of /info for the overview cards and the sidebar to render.
const makeInfo = (firmware: string, signature = 'mge_v3'): Partial<Info> => ({
  firmware,
  signature,
  serial_num: 1,
  system_voltage: 12,
  ethernet: { con_eth: true, ip: '192.168.1.10', mask: '255.255.255.0', gw: '192.168.1.1', mac: 'aa:bb:cc:dd:ee:ff' },
  wifi: {
    mode: 'ap', con_ap: 0, con_sta: false, con_sta_ssid: '', enabled: false,
    sta_ip: '', sta_mask: '', sta_gw: '', sta_mac: '', ap_ip: '192.168.4.1', ap_channel: 1, ap_mac: 'aa:bb:cc:dd:ee:00',
  },
  rs485_1: { is_busy: false, error_percentage: 0, server_connections_count: 0, port_mode: 'tcp_bridge', cache_enabled: false },
  rs485_2: { is_busy: false, error_percentage: 0, server_connections_count: 0, port_mode: 'disabled', cache_enabled: false },
});

const makeRs = () => ({
  baudrate: 9600 as const,
  parity: 'none' as const,
  stopbits: '1' as const,
  databits: '8' as const,
  term: false,
  fail_safe: false,
  tx_disabled: false,
  bridge: { mode: 'server' as const, ip: '', port: 502, modbus: true },
});

const makeSettings = (): Partial<Settings> => ({
  update_channel: 'stable',
  hostname: 'wb-mge',
  rs485_1: makeRs(),
  rs485_2: makeRs(),
});

const infoRef = ref<Partial<Info>>(makeInfo('1.0.0'));
const settingsRef = ref<Partial<Settings>>(makeSettings());

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: vi.fn().mockResolvedValue(undefined), startPolling: vi.fn(), stopPolling: vi.fn() }),
}));

vi.mock('@/common/settings', () => ({
  SettingsRefreshError: class SettingsRefreshError extends Error {},
  useSettings: () => ({
    data: settingsRef,
    initData: settingsRef,
    isChanged: () => false,
    isLoading: ref(false),
    partialRefresh: vi.fn().mockResolvedValue(undefined),
    updateSettings: vi.fn().mockResolvedValue(undefined),
    refresh: vi.fn(),
  }),
}));

vi.mock('@/utils/api', () => ({
  api: vi.fn().mockResolvedValue({}),
}));

vi.mock('@unhead/vue', () => ({
  injectHead: () => ({}),
  useHead: vi.fn(),
  createUnhead: vi.fn(() => ({})),
  headSymbol: Symbol('head'),
}));

const i18n = createI18n({ legacy: false, locale: 'en', messages, missingWarn: false, fallbackWarn: false });

const makeRouter = () => createRouter({
  history: createMemoryHistory(),
  routes: [
    { path: '/', component: { template: '<div/>' } },
    { path: '/logout', component: { template: '<div/>' } },
    { path: '/system', component: { template: '<div/>' } },
  ],
});

const mountDashboard = async () => {
  const { __resetChannelReleaseState } = await import('@/common/channelRelease');
  __resetChannelReleaseState();
  const { default: Dashboard } = await import('@/views/Dashboard.vue');
  const router = makeRouter();
  await router.push('/');
  await router.isReady();
  const wrapper = mount(Dashboard, {
    global: {
      plugins: [i18n, router],
      provide: { [matchedRouteKey as symbol]: shallowRef({ leaveGuards: new Set(), updateGuards: new Set() }) },
    },
  });
  await flushPromises();
  return wrapper;
};

const stubManifest = (text: string) => {
  const fetchMock = vi.fn().mockResolvedValue({ ok: true, status: 200, text: async () => text });
  vi.stubGlobal('fetch', fetchMock);
  return fetchMock;
};

// The firmware row of the gateway card, without the button next to it: the installed version plus
// the note in brackets.
const firmwareRowText = (wrapper: { find: (s: string) => { exists: () => boolean; text: () => string } }) => {
  const row = wrapper.find('.firmware-row .mono');
  return row.exists() ? row.text().replace(/\s+/g, ' ').trim() : '';
};

const updateButton = (wrapper: { findAll: (s: string) => { text: () => string }[] }) =>
  wrapper.findAll('button').find((button) => button.text() === 'Update');

beforeEach(() => {
  infoRef.value = makeInfo('1.0.0');
  settingsRef.value = makeSettings();
  vi.mocked(api).mockReset().mockResolvedValue({});
});

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('Dashboard.vue — firmware line', () => {
  it('DASH-I-001: names the channel and the version it offers, and offers the way to /system', async () => {
    const fetchMock = stubManifest(manifest);
    const wrapper = await mountDashboard();

    // The offered version is the one in the manifest's stable key, not whatever latest.txt used to
    // hold: exactly one request, and it goes to the manifest.
    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toContain('release-versions.yaml');
    expect(firmwareRowText(wrapper)).toBe('1.0.0 (in stable: 1.1.0)');
    expect(updateButton(wrapper)).toBeDefined();

    wrapper.unmount();
  });

  it('DASH-I-002: a device already on the channel version is told so, with nothing offered', async () => {
    infoRef.value = makeInfo('1.1.0');
    stubManifest(manifest);
    const wrapper = await mountDashboard();

    expect(firmwareRowText(wrapper)).toBe('1.1.0 (up to date)');
    // No version is named: naming it here would read as "1.1.0 (in stable: 1.1.0)" — the same
    // number twice, with nothing saying there is nothing to install.
    expect(wrapper.text()).not.toContain('in stable:');
    expect(updateButton(wrapper)).toBeUndefined();

    wrapper.unmount();
  });

  it('DASH-I-003: a check that could not be performed offers no version and no button', async () => {
    vi.stubGlobal('fetch', vi.fn().mockRejectedValue(new Error('offline')));
    vi.spyOn(console, 'error').mockImplementation(() => {});
    const wrapper = await mountDashboard();

    expect(firmwareRowText(wrapper)).toBe('1.0.0 (update check unavailable)');
    // Silently claiming "up to date" on a failed check is the one thing this line must never do.
    expect(wrapper.text()).not.toContain('up to date');
    expect(wrapper.text()).not.toContain('in stable:');
    expect(updateButton(wrapper)).toBeUndefined();

    wrapper.unmount();
    vi.mocked(console.error).mockRestore();
  });

  it('DASH-I-004: a board missing from the manifest gets its own sentence, not the failed-check one', async () => {
    infoRef.value = makeInfo('1.0.0', 'not_in_manifest');
    stubManifest(manifest);
    vi.spyOn(console, 'warn').mockImplementation(() => {});
    const wrapper = await mountDashboard();

    expect(firmwareRowText(wrapper)).toBe('1.0.0 (no update channels published for this board yet)');
    expect(wrapper.text()).not.toContain('update check unavailable');
    expect(updateButton(wrapper)).toBeUndefined();

    wrapper.unmount();
    vi.mocked(console.warn).mockRestore();
  });
});
