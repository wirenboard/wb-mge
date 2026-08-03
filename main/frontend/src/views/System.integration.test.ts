/**
 * Integration tests for the firmware card on System.vue — the channel selector, the two-channel
 * row and the update button. The channel-release composable is the real one; fw-releases (fetch)
 * and the device (api) are stubbed.
 *
 * SYS-I-001 — the offer comes from the manifest channel, not from latest.txt: exactly one fetch,
 *              to release-versions.yaml, and the button offers the stable version.
 * SYS-I-002 — the channels row shows BOTH versions from that single download, the selected channel
 *              is highlighted and the version equal to the installed one is marked "installed".
 * SYS-I-003 — a channel whose version cannot be read is shown as a dash while the other channel
 *              still resolves and the update button still works.
 * SYS-I-004 — a failed channel save shows an alert, puts the selector back to the saved value and
 *              leaves the offer untouched.
 * SYS-I-005 — both channels equal to the installed version: two "installed" marks, no button.
 * SYS-I-006 — a save the device accepted whose read-back failed keeps the selector on the new
 *              channel and reports the failed re-read, not a failed save.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { ref, shallowRef } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory, matchedRouteKey } from 'vue-router';
import { messages } from '@/i18n/messages';
import manifest from '@/common/__fixtures__/release-versions.sample.yaml?raw';
import type { Info, Settings } from '@/common/types';

// Enough of /info and /settings for the page and the sidebar to render.
const makeInfo = (firmware: string, signature = 'mge_v3'): Partial<Info> => ({
  firmware,
  signature,
  serial_num: 1,
  heap_free: 1024,
  heap_min_free: 512,
  heap_total: 4096,
  psram_available: false,
  psram_size_kb: 0,
  system_voltage: 12,
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
  login: 'admin',
  pass: '',
  web_port: 80,
  vout: true,
  rs485_1: makeRs(),
  rs485_2: makeRs(),
});

const infoRef = ref<Partial<Info>>(makeInfo('1.0.0'));
const settingsRef = ref<Partial<Settings>>(makeSettings());
const savedSettingsRef = ref<Partial<Settings>>(makeSettings());
const updateSettingsMock = vi.fn();
const partialRefreshMock = vi.fn();
const showAlertMock = vi.fn();

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: vi.fn().mockResolvedValue(undefined), startPolling: vi.fn(), stopPolling: vi.fn() }),
}));

// Declared through vi.hoisted so the mock factory (which is hoisted above the imports) and the test
// bodies share one class: instanceof is what tells a refused save from a failed read-back.
const { SettingsRefreshErrorMock } = vi.hoisted(() => ({
  SettingsRefreshErrorMock: class SettingsRefreshError extends Error {},
}));

vi.mock('@/common/settings', () => ({
  SettingsRefreshError: SettingsRefreshErrorMock,
  useSettings: () => ({
    data: settingsRef,
    initData: savedSettingsRef,
    isChanged: () => false,
    isLoading: ref(false),
    partialRefresh: partialRefreshMock,
    updateSettings: updateSettingsMock,
    refresh: vi.fn(),
  }),
}));

vi.mock('@/common/alert', () => ({
  useAlerts: () => ({ alerts: [], showAlert: showAlertMock }),
}));

vi.mock('@/utils/api', () => ({
  api: vi.fn().mockResolvedValue({}),
}));

// @/i18n reads localStorage at module scope, which happy-dom does not provide here.
vi.mock('@/i18n', () => ({
  changeLang: vi.fn(),
  languages: { en: 'English', ru: 'Русский', kk: 'Қазақша', it: 'Italiano', de: 'Deutsch' },
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

const mountSystem = async () => {
  const { __resetChannelReleaseState } = await import('@/common/channelRelease');
  __resetChannelReleaseState();
  const { default: System } = await import('@/views/System.vue');
  const router = makeRouter();
  await router.push('/system');
  await router.isReady();
  const wrapper = mount(System, {
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

beforeEach(() => {
  infoRef.value = makeInfo('1.0.0');
  settingsRef.value = makeSettings();
  savedSettingsRef.value = makeSettings();
  updateSettingsMock.mockReset().mockResolvedValue(undefined);
  partialRefreshMock.mockReset().mockResolvedValue(undefined);
  showAlertMock.mockReset();
});

afterEach(() => {
  vi.unstubAllGlobals();
});

describe('System.vue — firmware channels', () => {
  it('SYS-I-001: offers the version of the selected channel after a single manifest request', async () => {
    const fetchMock = stubManifest(manifest);
    const wrapper = await mountSystem();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toContain('release-versions.yaml');
    // No request to latest.txt anywhere.
    expect(fetchMock.mock.calls.some((call) => String(call[0]).includes('latest'))).toBe(false);
    expect(wrapper.text()).toContain('Update to 1.1.0');

    wrapper.unmount();
  });

  it('SYS-I-002: shows both channels, marks the selected one and the installed one', async () => {
    infoRef.value = makeInfo('1.1.1');
    stubManifest(manifest);
    const wrapper = await mountSystem();

    const row = wrapper.findAll('.firmware-channels');
    expect(row).toHaveLength(1);
    const text = row[0].text();
    expect(text).toContain('stable: 1.1.0');
    expect(text).toContain('testing: 1.1.1');
    // The installed version (testing) carries the mark, the other one does not.
    const marks = row[0].findAll('.firmware-note');
    expect(marks).toHaveLength(1);
    expect(marks[0].text()).toBe('installed');
    // The selected channel is the highlighted one.
    const selected = row[0].findAll('.firmware-channel-selected');
    expect(selected).toHaveLength(1);
    expect(selected[0].text()).toContain('stable');

    wrapper.unmount();
  });

  it('SYS-I-003: an unreadable channel version is a dash, the other channel still works', async () => {
    const mixed = manifest.replace(
      '    testing: fw/by-signature/mge_v3/main/1.1.1.bin',
      '    testing: fw/by-signature/mge_v3/main/MF1.03D.compfw',
    );
    stubManifest(mixed);
    const wrapper = await mountSystem();

    const text = wrapper.find('.firmware-channels').text();
    expect(text).toContain('stable: 1.1.0');
    expect(text).toContain('testing: —');
    expect(wrapper.text()).toContain('Update to 1.1.0');

    wrapper.unmount();
  });

  it('SYS-I-004: a failed channel save alerts and puts the selector back', async () => {
    stubManifest(manifest);
    updateSettingsMock.mockRejectedValue(new Error('connection_error'));
    const wrapper = await mountSystem();

    const select = wrapper.find('#update_channel');
    await select.setValue('testing');
    await flushPromises();

    expect(updateSettingsMock).toHaveBeenCalledWith({ update_channel: 'testing' });
    expect(showAlertMock).toHaveBeenCalled();
    expect(settingsRef.value.update_channel).toBe('stable');
    // The offer still names the stable version: nothing was recomputed for a channel the device
    // never accepted.
    expect(wrapper.text()).toContain('Update to 1.1.0');

    wrapper.unmount();
  });

  it('SYS-I-006: a save whose read-back failed keeps the channel and says what really failed', async () => {
    stubManifest(manifest);
    // The device accepted the channel; only the /settings read that follows the POST did not come
    // back. Rolling the selector back here would show a channel the device no longer has.
    updateSettingsMock.mockRejectedValue(new SettingsRefreshErrorMock('connection_error'));
    const wrapper = await mountSystem();

    const select = wrapper.find('#update_channel');
    await select.setValue('testing');
    await flushPromises();

    expect(settingsRef.value.update_channel).toBe('testing');
    expect(showAlertMock).toHaveBeenCalledWith(
      expect.stringContaining('offer could not be refreshed'),
      expect.objectContaining({ type: 'warning' }),
    );

    wrapper.unmount();
  });

  it('SYS-I-005: when both channels are already installed there is no update button', async () => {
    const same = manifest.replace(
      '    testing: fw/by-signature/mge_v3/main/1.1.1.bin',
      '    testing: fw/by-signature/mge_v3/main/1.1.0.bin',
    );
    infoRef.value = makeInfo('1.1.0');
    stubManifest(same);
    const wrapper = await mountSystem();

    expect(wrapper.find('.firmware-channels').findAll('.firmware-note')).toHaveLength(2);
    expect(wrapper.text()).not.toContain('Update to');
    expect(wrapper.text()).toContain('in stable: 1.1.0');

    wrapper.unmount();
  });
});
