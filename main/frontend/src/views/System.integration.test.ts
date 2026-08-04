/**
 * Integration tests for the firmware card on System.vue — the channel selector, the update row and
 * its status line. The channel-release composable is the real one; fw-releases (fetch) and the
 * device (api) are stubbed.
 *
 * SYS-I-001 — the offer comes from the manifest channel, not from latest.txt: exactly one fetch,
 *              to release-versions.yaml, and the update button is offered.
 * SYS-I-002 — the channel dropdown carries BOTH versions from that single download in its option
 *              labels, the selected channel is the value of the select and the version equal to the
 *              installed one is marked "installed".
 * SYS-I-003 — a channel whose version cannot be read is shown as a dash while the other channel
 *              still resolves and the update button still works.
 * SYS-I-004 — a failed channel save shows an alert, puts the selector back to the saved value and
 *              leaves the offer untouched.
 * SYS-I-005 — both channels equal to the installed version: two "installed" marks, no button and
 *              no status line repeating the version.
 * SYS-I-006 — a save the device accepted whose read-back failed keeps the selector on the new
 *              channel and reports the failed re-read, not a failed save.
 * SYS-I-007 — switching the channel costs exactly one POST /settings and no second manifest
 *              request, and the offer follows the new channel.
 * SYS-I-008 — a manifest that could not be downloaded: channel names without a version suffix,
 *              "update check unavailable" in the status line, no update button, re-check offered.
 * SYS-I-009 — a board with no published channels gets its own sentence, not the failed-check one.
 * SYS-I-010 — the status line carries the phase texts: download percent, reboot, "updated to X",
 *              "the version did not change".
 * SYS-I-011 — in `conflict` there is no update button, the conflict sentence is in the status line
 *              and "Check state" is the only way forward.
 * SYS-I-012 — a manual upload in flight disables the update button and the channel selector.
 * SYS-I-013 — the manual install posts to /update and reports the 409 conflict, not a generic
 *              update error.
 */

import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest';
import { mount, flushPromises } from '@vue/test-utils';
import { nextTick, ref, shallowRef } from 'vue';
import { createI18n } from 'vue-i18n';
import { createRouter, createMemoryHistory, matchedRouteKey } from 'vue-router';
import { useFirmware } from '@/common/firmware';
import { messages } from '@/i18n/messages';
import manifest from '@/common/__fixtures__/release-versions.sample.yaml?raw';
import { api } from '@/utils/api';
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

// The phases the card renders are produced by a long device conversation (download, upload, reboot,
// verify). The tests that only check what a phase looks like drive the composable's own state
// instead of replaying that conversation — it is the very state the card reads.
const channelReleaseState = async () => {
  const { useChannelRelease } = await import('@/common/channelRelease');
  return useChannelRelease();
};

const channelLabels = (wrapper: { findAll: (s: string) => { text: () => string }[] }) =>
  wrapper.findAll('#update_channel option').map((option) => option.text().replace(/\s+/g, ' ').trim());

const statusText = (wrapper: { find: (s: string) => { exists: () => boolean; text: () => string } }) => {
  const status = wrapper.find('.firmware-update-status');
  return status.exists() ? status.text() : '';
};

beforeEach(() => {
  infoRef.value = makeInfo('1.0.0');
  settingsRef.value = makeSettings();
  savedSettingsRef.value = makeSettings();
  updateSettingsMock.mockReset().mockResolvedValue(undefined);
  partialRefreshMock.mockReset().mockResolvedValue(undefined);
  showAlertMock.mockReset();
  vi.mocked(api).mockReset().mockResolvedValue({});
  // Module state of useFirmware: a test that leaves an upload "in flight" would disable the card
  // for every test after it.
  useFirmware().isUpdating.value = false;
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
    // The label carries no version any more: the offer is read from the channel row.
    expect(wrapper.text()).toContain('Update to last version on channel');
    expect(channelLabels(wrapper)).toContain('Stable (last 1.1.0)');

    wrapper.unmount();
  });

  it('SYS-I-002: both options carry their version, the selected one is the saved channel', async () => {
    infoRef.value = makeInfo('1.1.1');
    stubManifest(manifest);
    const wrapper = await mountSystem();

    // Both versions come out of the one download, so the closed dropdown already answers "what is
    // in this channel" and opening it answers it for the other one.
    expect(channelLabels(wrapper)).toEqual(['Stable (last 1.1.0)', 'Testing (installed 1.1.1)']);
    expect((wrapper.find('#update_channel').element as HTMLSelectElement).value).toBe('stable');

    wrapper.unmount();
  });

  it('SYS-I-003: an unreadable channel version is a dash, the other channel still works', async () => {
    const mixed = manifest.replace(
      '    testing: fw/by-signature/mge_v3/main/1.1.1.bin',
      '    testing: fw/by-signature/mge_v3/main/MF1.03D.compfw',
    );
    stubManifest(mixed);
    const wrapper = await mountSystem();

    expect(channelLabels(wrapper)).toEqual(['Stable (last 1.1.0)', 'Testing (last —)']);
    expect(wrapper.text()).toContain('Update to last version on channel');

    wrapper.unmount();
  });

  it('SYS-I-004: a failed channel save alerts and puts the selector back', async () => {
    stubManifest(manifest);
    updateSettingsMock.mockRejectedValue(new Error('connection_error'));
    const wrapper = await mountSystem();

    await wrapper.find('#update_channel').setValue('testing');
    await flushPromises();

    expect(updateSettingsMock).toHaveBeenCalledWith({ update_channel: 'testing' });
    expect(showAlertMock).toHaveBeenCalled();
    expect(settingsRef.value.update_channel).toBe('stable');
    // The offer must not have been recomputed for a channel the device never accepted: a check()
    // run before (or regardless of) the roll-back would leave the card offering 1.1.1 — and the
    // confirm dialog naming it — while the selector says Stable. The selector value itself is
    // bound with v-model, so asserting it would only restate the line above.
    const state = await channelReleaseState();
    expect(state.resolvedChannel.value).toBe('stable');
    expect(state.release.value).toEqual({ ok: true, version: '1.1.0', url: expect.stringContaining('1.1.0.bin') });

    wrapper.unmount();
  });

  it('SYS-I-006: a save whose read-back failed keeps the channel and says what really failed', async () => {
    stubManifest(manifest);
    // The device accepted the channel; only the /settings read that follows the POST did not come
    // back. Rolling the selector back here would show a channel the device no longer has.
    updateSettingsMock.mockRejectedValue(new SettingsRefreshErrorMock('connection_error'));
    const wrapper = await mountSystem();

    await wrapper.find('#update_channel').setValue('testing');
    await flushPromises();

    expect(settingsRef.value.update_channel).toBe('testing');
    expect(showAlertMock).toHaveBeenCalledWith(
      expect.stringContaining('offer could not be refreshed'),
      expect.objectContaining({ type: 'warning' }),
    );

    wrapper.unmount();
  });

  it('SYS-I-005: when both channels are already installed there is no update button and no status', async () => {
    const same = manifest.replace(
      '    testing: fw/by-signature/mge_v3/main/1.1.1.bin',
      '    testing: fw/by-signature/mge_v3/main/1.1.0.bin',
    );
    infoRef.value = makeInfo('1.1.0');
    stubManifest(same);
    const wrapper = await mountSystem();

    expect(channelLabels(wrapper)).toEqual(['Stable (installed 1.1.0)', 'Testing (installed 1.1.0)']);
    expect(wrapper.text()).not.toContain('Update to last version on channel');
    // The version the channel offers is in the channel row only — no second copy in the status.
    expect(statusText(wrapper)).toBe('');

    wrapper.unmount();
  });

  it('SYS-I-007: switching the channel costs one POST and no second manifest download', async () => {
    // Installed = the stable version, so the card starts with nothing to offer.
    infoRef.value = makeInfo('1.1.0');
    const fetchMock = stubManifest(manifest);
    const wrapper = await mountSystem();

    expect(wrapper.text()).not.toContain('Update to last version on channel');

    await wrapper.find('#update_channel').setValue('testing');
    await flushPromises();

    expect(updateSettingsMock).toHaveBeenCalledTimes(1);
    expect(updateSettingsMock).toHaveBeenCalledWith({ update_channel: 'testing' });
    expect(fetchMock).toHaveBeenCalledTimes(1);
    // The offer follows the channel the device confirmed.
    expect(wrapper.text()).toContain('Update to last version on channel');
    expect(channelLabels(wrapper)).toEqual(['Stable (installed 1.1.0)', 'Testing (last 1.1.1)']);

    wrapper.unmount();
  });

  it('SYS-I-008: an undownloadable manifest leaves the channels bare and says the check failed', async () => {
    const fetchMock = vi.fn().mockRejectedValue(new Error('offline'));
    vi.stubGlobal('fetch', fetchMock);
    const wrapper = await mountSystem();

    // No suffix at all: "last —" would claim a lookup that never happened.
    expect(channelLabels(wrapper)).toEqual(['Stable', 'Testing']);
    expect(statusText(wrapper)).toContain('update check unavailable');
    expect(wrapper.text()).not.toContain('Update to last version on channel');
    expect(statusText(wrapper)).toContain('Check state');

    wrapper.unmount();
  });

  it('SYS-I-009: a board without published channels gets its own sentence', async () => {
    infoRef.value = makeInfo('1.0.0', 'not_in_manifest');
    stubManifest(manifest);
    const wrapper = await mountSystem();

    expect(statusText(wrapper)).toContain('no update channels published for this board yet');
    expect(statusText(wrapper)).not.toContain('update check unavailable');
    expect(channelLabels(wrapper)).toEqual(['Stable', 'Testing']);

    wrapper.unmount();
  });

  it('SYS-I-010: the status line carries the phase texts', async () => {
    stubManifest(manifest);
    const wrapper = await mountSystem();
    const state = await channelReleaseState();

    state.phase.value = 'downloading';
    state.progress.value = 42;
    await nextTick();
    expect(statusText(wrapper)).toContain('downloading… 42%');

    state.phase.value = 'rebooting';
    await nextTick();
    expect(statusText(wrapper)).toContain('the device is rebooting…');

    state.phase.value = 'verified';
    state.message.value = '1.1.0';
    await nextTick();
    expect(statusText(wrapper)).toContain('updated to 1.1.0');

    state.phase.value = 'not_applied';
    await nextTick();
    expect(statusText(wrapper)).toContain('the version did not change');

    state.phase.value = 'idle';
    state.message.value = null;
    wrapper.unmount();
  });

  it('SYS-I-011: in conflict the card offers a re-check instead of an update', async () => {
    stubManifest(manifest);
    const wrapper = await mountSystem();
    const state = await channelReleaseState();

    state.phase.value = 'conflict';
    await nextTick();

    expect(statusText(wrapper)).toContain('An update is already running, the device will reboot shortly');
    expect(wrapper.text()).not.toContain('Update to last version on channel');
    expect(statusText(wrapper)).toContain('Check state');

    state.phase.value = 'idle';
    wrapper.unmount();
  });

  it('SYS-I-012: a manual upload in flight disables the update button and the channel selector', async () => {
    stubManifest(manifest);
    const wrapper = await mountSystem();

    useFirmware().isUpdating.value = true;
    await nextTick();

    const updateButton = wrapper.findAll('button').find((button) => button.text() === 'Update to last version on channel');
    expect(updateButton).toBeDefined();
    expect(updateButton!.attributes()).toHaveProperty('disabled');
    expect(wrapper.find('#update_channel').attributes()).toHaveProperty('disabled');

    wrapper.unmount();
  });

  it('SYS-I-013: the manual install posts to /update and reports a 409 as a conflict', async () => {
    stubManifest(manifest);
    const wrapper = await mountSystem();

    // Only the upload is refused: the card keeps reading /uptime while the alert is shown.
    vi.mocked(api).mockImplementation(async (path: string) => {
      if (path === 'update') {
        throw Object.assign(new Error('http_error'), { response: { status: 409 } });
      }
      return {} as never;
    });

    const fileInput = wrapper.find('input[type="file"]');
    Object.defineProperty(fileInput.element, 'files', {
      value: [new File([new Uint8Array([0xe9])], 'firmware.bin')],
      configurable: true,
    });
    await fileInput.trigger('change');
    const uploadButton = wrapper.findAll('.fileUpload-wrapper button').at(-1);
    await uploadButton!.trigger('click');
    await flushPromises();

    expect(vi.mocked(api)).toHaveBeenCalledWith('update', expect.objectContaining({ method: 'POST' }));
    expect(showAlertMock).toHaveBeenCalledWith(
      'An update is already running, the device will reboot shortly',
      expect.objectContaining({ type: 'error' }),
    );
    expect(showAlertMock).not.toHaveBeenCalledWith('Firmware update error', expect.anything());

    wrapper.unmount();
  });
});
