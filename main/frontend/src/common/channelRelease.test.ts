/**
 * Unit tests for the channel-release composable: the offer, the one-click install and the
 * post-reboot verdict. fetch (fw-releases) and api (the device) are both stubbed.
 *
 * CR-001 — the manifest is downloaded once per session; a channel switch re-resolves the copy in
 *           memory without a second request, and both views share the same offer.
 * CR-002 — an empty signature is "check unavailable", not "no channels published".
 * CR-003 — a manifest that answers 500 or hangs past MANIFEST_TIMEOUT_MS is "check unavailable".
 * CR-004 — a truncated manifest and an intact manifest without our signature are distinguishable.
 * CR-005 — install: one GET, one POST of the same length, uptime drops, /info shows the target
 *           version -> verified.
 * CR-006 — the device comes back on the OLD version -> not applied (no silent re-offer).
 * CR-007 — a 404 (HTML body), a length mismatch and a wrong magic byte all abort before the POST.
 * CR-008 — a 409 from the device puts the operation in `conflict`; only recheck() leaves it.
 * CR-009 — a stalled download is aborted after FW_DOWNLOAD_STALL_MS and never uploaded.
 * CR-010 — the device that does not come back is reported as such; a growing uptime is not a reboot.
 * CR-011 — the device version changed between the check and the click -> nothing is installed.
 * CR-012 — version suffixes are not collapsed: 1.0.0-rc29 != 1.0.0.
 * CR-013 — a channel whose version cannot be read is null while the other channel still works.
 * CR-014 — a click on an offer that moved to another installable version says so, and the next
 *           check clears the note.
 * CR-015 — a recompute that leaves nothing to install shows no "press update again" note.
 * CR-016 — a recheck that failed in `conflict` is announced, and a later check clears the note.
 * CR-017 — a download over FW_DOWNLOAD_MAX_BYTES is refused, before the body is read when the
 *           response declares its size.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { ref } from 'vue';
import manifest from './__fixtures__/release-versions.sample.yaml?raw';
import type { Info, Settings, UpdateChannel } from '@/common/types';

const infoRef = ref<Partial<Info> | undefined>(undefined);
const settingsRef = ref<Partial<Settings> | undefined>(undefined);
const fetchInfoMock = vi.fn();
const partialRefreshMock = vi.fn();
const apiMock = vi.fn();

vi.mock('@/common/info', () => ({
  useInfo: () => ({ info: infoRef, fetchInfo: fetchInfoMock }),
}));

vi.mock('@/common/settings', () => ({
  useSettings: () => ({ data: settingsRef, partialRefresh: partialRefreshMock }),
}));

vi.mock('@/common/uptime', () => ({
  isReconnecting: ref(false),
  useUptime: () => ({ isReconnecting: ref(false) }),
}));

vi.mock('@/utils/api', () => ({
  api: (url: string, options?: Record<string, unknown>) => apiMock(url, options),
}));

import { FW_DOWNLOAD_MAX_BYTES, FW_DOWNLOAD_STALL_MS, MANIFEST_TIMEOUT_MS, REBOOT_SETTLE_MS, VERIFY_BUDGET_MS, VERIFY_POLL_MS, __resetChannelReleaseState, useChannelRelease } from './channelRelease';

const BIN_URL = 'https://fw-releases.wirenboard.com/fw/by-signature/mge_v3/main/1.1.0.bin';

// A minimal firmware image: the ESP magic byte plus padding.
const image = (magic = 0xe9, size = 64) => {
  const bytes = new Uint8Array(size);
  bytes[0] = magic;
  return bytes;
};

const textResponse = (text: string, status = 200) => ({
  ok: status >= 200 && status < 300,
  status,
  text: async () => text,
});

type BinaryOptions = { status?: number; contentLength?: number | null; stallAfterFirst?: boolean };

const binaryResponse = (chunks: Uint8Array[], options: BinaryOptions = {}) => {
  const status = options.status ?? 200;
  const total = chunks.reduce((sum, chunk) => sum + chunk.length, 0);
  const declared = options.contentLength === undefined ? total : options.contentLength;
  let index = 0;
  return {
    ok: status >= 200 && status < 300,
    status,
    headers: {
      get: (name: string) => (name.toLowerCase() === 'content-length' && declared !== null ? String(declared) : null),
    },
    body: {
      getReader: () => ({
        read: async () => {
          if (index < chunks.length) {
            index += 1;
            return { done: false, value: chunks[index - 1] };
          }
          if (options.stallAfterFirst) {
            // The server stopped sending but did not close the socket.
            return new Promise(() => {});
          }
          return { done: true, value: undefined };
        },
      }),
    },
  };
};

// A response that declares a body far past FW_DOWNLOAD_MAX_BYTES. The reader is a spy: the whole
// point of the declared-size check is that nothing is ever read from it.
const oversizedResponse = (declared: number) => {
  const read = vi.fn(async () => ({ done: true, value: undefined }));
  return {
    read,
    response: {
      ok: true,
      status: 200,
      headers: { get: (name: string) => (name.toLowerCase() === 'content-length' ? String(declared) : null) },
      body: { getReader: () => ({ read }) },
    },
  };
};

// Simulated device state: what /info, /uptime and /update answer.
let deviceFirmware = '1.0.0';
let deviceSignature = 'mge_v3';
let deviceOffline = false;
let uptimeSeconds = 3600;
let updateOutcome: 'ok' | 409 | 'device-error' = 'ok';
const uploadedSizes: number[] = [];

const setDevice = (firmware: string, signature = 'mge_v3') => {
  deviceFirmware = firmware;
  deviceSignature = signature;
  infoRef.value = { firmware, signature };
};

const setChannel = (channel: UpdateChannel) => {
  settingsRef.value = { update_channel: channel };
};

beforeEach(() => {
  vi.useFakeTimers();
  __resetChannelReleaseState();
  uptimeSeconds = 3600;
  updateOutcome = 'ok';
  uploadedSizes.length = 0;
  deviceOffline = false;
  fetchInfoMock.mockReset().mockImplementation(async () => {
    if (deviceOffline) {
      throw new Error('device is offline');
    }
    infoRef.value = { firmware: deviceFirmware, signature: deviceSignature };
  });
  partialRefreshMock.mockReset().mockResolvedValue(undefined);
  apiMock.mockReset().mockImplementation(async (url: string, options?: Record<string, unknown>) => {
    if (url === 'uptime') {
      if (deviceOffline) {
        throw new Error('device is offline');
      }
      return {
        days: Math.floor(uptimeSeconds / 86400),
        hours: Math.floor((uptimeSeconds % 86400) / 3600),
        minutes: Math.floor((uptimeSeconds % 3600) / 60),
        seconds: uptimeSeconds % 60,
      };
    }
    if (url === 'update') {
      uploadedSizes.push((options?.body as Blob).size);
      if (updateOutcome === 409) {
        throw { response: { status: 409 } };
      }
      if (updateOutcome === 'device-error') {
        throw {
          response: {
            status: 400,
            clone: () => ({ json: async () => ({ success: false, error: 'Invalid OTA firmware' }) }),
          },
        };
      }
      return { success: true, message: 'ok', bytes_written: 64 };
    }
    throw new Error(`unexpected api call: ${url}`);
  });
  setDevice('1.0.0');
  setChannel('stable');
  vi.spyOn(console, 'warn').mockImplementation(() => {});
  vi.spyOn(console, 'error').mockImplementation(() => {});
});

afterEach(() => {
  vi.unstubAllGlobals();
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('useChannelRelease — checking', () => {
  it('CR-001: downloads the manifest once and offers the stable version', async () => {
    const fetchMock = vi.fn().mockResolvedValue(textResponse(manifest));
    vi.stubGlobal('fetch', fetchMock);

    const { check, phase, release, channels } = useChannelRelease();
    await check();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock.mock.calls[0][0]).toContain('release-versions.yaml');
    expect(phase.value).toBe('available');
    expect(release.value).toEqual({ ok: true, version: '1.1.0', url: BIN_URL });
    // CR-013: both channels are shown from the same download.
    expect(channels.value?.stable?.version).toBe('1.1.0');
    expect(channels.value?.testing?.version).toBe('1.1.1');
  });

  it('CR-001: a second view mounting re-uses the manifest already in memory', async () => {
    const fetchMock = vi.fn().mockResolvedValue(textResponse(manifest));
    vi.stubGlobal('fetch', fetchMock);

    const first = useChannelRelease();
    await first.check();
    const second = useChannelRelease();
    await second.check();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    // The offer is one shared piece of state, so both views show the same version.
    expect(second.release.value).toEqual(first.release.value);
  });

  it('CR-001: switching the channel re-resolves without downloading the manifest again', async () => {
    const fetchMock = vi.fn().mockResolvedValue(textResponse(manifest));
    vi.stubGlobal('fetch', fetchMock);

    const { check, release, resolvedChannel } = useChannelRelease();
    await check();
    setChannel('testing');
    await check();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(resolvedChannel.value).toBe('testing');
    expect(release.value).toEqual({
      ok: true,
      version: '1.1.1',
      url: 'https://fw-releases.wirenboard.com/fw/by-signature/mge_v3/main/1.1.1.bin',
    });
  });

  it('CR-002: an empty signature is an unavailable check, and no manifest is requested', async () => {
    const fetchMock = vi.fn().mockResolvedValue(textResponse(manifest));
    vi.stubGlobal('fetch', fetchMock);
    setDevice('1.0.0', '');

    const { check, phase, release } = useChannelRelease();
    await check();

    expect(fetchMock).not.toHaveBeenCalled();
    expect(phase.value).toBe('unavailable');
    expect(release.value).toEqual({ ok: false, reason: 'unavailable' });
  });

  it('CR-003: a manifest answering 500 leaves the check unavailable', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(textResponse('nope', 500)));

    const { check, phase, release } = useChannelRelease();
    await check();

    expect(phase.value).toBe('unavailable');
    expect(release.value).toEqual({ ok: false, reason: 'unavailable' });
  });

  it('CR-003: a manifest that never answers is aborted after MANIFEST_TIMEOUT_MS', async () => {
    const fetchMock = vi.fn().mockImplementation((_url: string, init: { signal: AbortSignal }) =>
      new Promise((_resolve, reject) => {
        init.signal.addEventListener('abort', () => reject(new Error('aborted')));
      }));
    vi.stubGlobal('fetch', fetchMock);

    const { check, phase } = useChannelRelease();
    const pending = check();
    await vi.advanceTimersByTimeAsync(MANIFEST_TIMEOUT_MS + 1);
    await pending;

    expect(phase.value).toBe('unavailable');
  });

  it('CR-004: a truncated manifest is unavailable, an intact one without us is no-signature', async () => {
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(textResponse('releases:\n')));
    const { check, phase, release } = useChannelRelease();
    await check();
    expect(phase.value).toBe('unavailable');
    expect(release.value).toEqual({ ok: false, reason: 'unavailable' });

    __resetChannelReleaseState();
    const intact = 'releases:\n  mio:\n    stable: fw/by-signature/mio/main/1.8.4.wbfw\n';
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(textResponse(intact)));
    await check();
    expect(phase.value).toBe('unavailable');
    expect(release.value).toEqual({ ok: false, reason: 'no-signature' });
  });

  it('CR-012: version suffixes are compared as whole strings', async () => {
    const rcManifest = manifest.replace(
      '    stable: fw/by-signature/mge_v3/main/1.1.0.bin',
      '    stable: fw/by-signature/mge_v3/main/1.0.0-rc29.bin',
    );
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(textResponse(rcManifest)));
    setDevice('1.0.0-rc29');

    const { check, phase } = useChannelRelease();
    await check();
    expect(phase.value).toBe('up_to_date');

    __resetChannelReleaseState();
    setDevice('1.0.0');
    await check();
    expect(phase.value).toBe('available');
  });

  it('CR-013: a channel with an unreadable version is null, the other one still resolves', async () => {
    const mixed = manifest.replace(
      '    testing: fw/by-signature/mge_v3/main/1.1.1.bin',
      '    testing: fw/by-signature/mge_v3/main/MF1.03D.compfw',
    );
    vi.stubGlobal('fetch', vi.fn().mockResolvedValue(textResponse(mixed)));

    const { check, phase, channels, release } = useChannelRelease();
    await check();

    expect(channels.value?.testing).toBeNull();
    expect(channels.value?.stable?.version).toBe('1.1.0');
    expect(phase.value).toBe('available');
    expect(release.value).toEqual({ ok: true, version: '1.1.0', url: BIN_URL });
  });
});

describe('useChannelRelease — installing', () => {
  const prepare = async (fetchMock: ReturnType<typeof vi.fn>) => {
    vi.stubGlobal('fetch', fetchMock);
    const composable = useChannelRelease();
    await composable.check();
    return composable;
  };

  const manifestThen = (...responses: unknown[]) => {
    const mock = vi.fn();
    mock.mockResolvedValueOnce(textResponse(manifest));
    responses.forEach((response) => mock.mockResolvedValueOnce(response));
    return mock;
  };

  it('CR-005: downloads once, uploads the same bytes and verifies the new version', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase, message } = await prepare(fetchMock);

    const pending = install();
    await vi.advanceTimersByTimeAsync(1);
    // The device reboots: uptime drops and /info starts reporting the new version.
    deviceFirmware = '1.1.0';
    uptimeSeconds = 5;
    await vi.advanceTimersByTimeAsync(REBOOT_SETTLE_MS + VERIFY_POLL_MS * 2);
    await pending;

    expect(fetchMock).toHaveBeenCalledTimes(2);
    expect(fetchMock.mock.calls[1][0]).toBe(BIN_URL);
    expect(uploadedSizes).toEqual([64]);
    expect(phase.value).toBe('verified');
    expect(message.value).toBe('1.1.0');
  });

  it('CR-006: the device coming back on the old version is reported as not applied', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase } = await prepare(fetchMock);

    const pending = install();
    await vi.advanceTimersByTimeAsync(1);
    uptimeSeconds = 5;
    await vi.advanceTimersByTimeAsync(REBOOT_SETTLE_MS + VERIFY_POLL_MS * 2);
    await pending;

    expect(uploadedSizes).toHaveLength(1);
    expect(phase.value).toBe('not_applied');
  });

  it('CR-007: a 404 with an HTML body never reaches the device', async () => {
    const fetchMock = manifestThen({
      ok: false,
      status: 404,
      headers: { get: () => null },
      body: null,
    });
    const { install, phase, failure, message } = await prepare(fetchMock);

    await install();

    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('download');
    expect(message.value).toContain('404');
  });

  it('CR-007: a body shorter than Content-Length never reaches the device', async () => {
    const fetchMock = manifestThen(binaryResponse([image()], { contentLength: 4096 }));
    const { install, phase, failure } = await prepare(fetchMock);

    await install();

    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('download');
  });

  it('CR-007: a body that is not an ESP image never reaches the device', async () => {
    const fetchMock = manifestThen(binaryResponse([image(0x3c)]));
    const { install, phase, message } = await prepare(fetchMock);

    await install();

    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('failed');
    expect(message.value).toContain('ESP image');
  });

  it('CR-007: a failed attempt can be retried, unlike a conflict', async () => {
    const fetchMock = manifestThen(
      { ok: false, status: 404, headers: { get: () => null }, body: null },
      binaryResponse([image()]),
    );
    const { install, phase, canInstall } = await prepare(fetchMock);

    await install();
    expect(phase.value).toBe('failed');
    expect(canInstall.value).toBe(true);

    const pending = install();
    await vi.advanceTimersByTimeAsync(1);
    deviceFirmware = '1.1.0';
    uptimeSeconds = 5;
    await vi.advanceTimersByTimeAsync(REBOOT_SETTLE_MS + VERIFY_POLL_MS * 2);
    await pending;

    expect(phase.value).toBe('verified');
  });

  it('CR-008: a 409 puts the operation into conflict, and recheck() is the way out', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, recheck, phase, canInstall } = await prepare(fetchMock);
    updateOutcome = 409;

    await install();
    expect(phase.value).toBe('conflict');
    expect(canInstall.value).toBe(false);

    // A repeated install is refused while in conflict.
    await install();
    expect(uploadedSizes).toHaveLength(1);

    await recheck();
    expect(phase.value).toBe('available');
    expect(fetchInfoMock).toHaveBeenCalled();
    expect(partialRefreshMock).toHaveBeenCalledWith(['update_channel']);
  });

  it('CR-008: a device-side refusal shows the reason the device sent', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase, failure, message } = await prepare(fetchMock);
    updateOutcome = 'device-error';

    await install();

    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('upload');
    expect(message.value).toBe('Invalid OTA firmware');
  });

  it('CR-009: a stalled download is aborted and never uploaded', async () => {
    const fetchMock = manifestThen(binaryResponse([image()], { contentLength: 4096, stallAfterFirst: true }));
    const { install, phase, failure } = await prepare(fetchMock);

    const pending = install();
    await vi.advanceTimersByTimeAsync(FW_DOWNLOAD_STALL_MS + 1);
    await pending;

    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('download');
  });

  it('CR-010: a device that does not come back is reported, with no false "updated"', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase, failure } = await prepare(fetchMock);

    const pending = install();
    await vi.advanceTimersByTimeAsync(1);
    deviceOffline = true; // the device never comes back after the upload
    await vi.advanceTimersByTimeAsync(REBOOT_SETTLE_MS + VERIFY_BUDGET_MS + VERIFY_POLL_MS);
    await pending;

    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('no_response');
  });

  it('CR-010: a growing uptime does not count as a reboot', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase, failure } = await prepare(fetchMock);
    // The device answers the whole time, but its uptime keeps growing: it never rebooted.
    const pending = install();
    await vi.advanceTimersByTimeAsync(1);
    deviceFirmware = '1.1.0';
    const grow = setInterval(() => {
      uptimeSeconds += 2;
    }, VERIFY_POLL_MS);
    await vi.advanceTimersByTimeAsync(REBOOT_SETTLE_MS + VERIFY_BUDGET_MS + VERIFY_POLL_MS);
    clearInterval(grow);
    await pending;

    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('no_response');
  });

  it('CR-011: a version change between the check and the click cancels the install', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase } = await prepare(fetchMock);
    // Another tab already updated the device to the version this tab was about to install.
    deviceFirmware = '1.1.0';

    await install();

    expect(fetchMock).toHaveBeenCalledTimes(1); // the firmware was never requested
    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('up_to_date');
  });

  it('CR-011: a channel change between the check and the click cancels the install', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase, release } = await prepare(fetchMock);
    partialRefreshMock.mockImplementation(async () => {
      setChannel('testing');
    });

    await install();

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('available');
    expect(release.value).toMatchObject({ version: '1.1.1' });
  });

  it('CR-014: a click on an offer that moved to another version says so, and a check clears it', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, check, phase, notice } = await prepare(fetchMock);
    // The channel was switched in another tab between the check and the click, so the recomputed
    // offer is a different version — but one the user can still install by pressing again.
    partialRefreshMock.mockImplementation(async () => {
      setChannel('testing');
    });

    await install();

    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('available');
    expect(notice.value).toBe('offer_changed');

    // The note belongs to the click that produced it and must not outlive the next check.
    await check();
    expect(notice.value).toBeNull();
  });

  it('CR-015: an offer that recomputed to up_to_date carries no "press update again" note', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, phase, notice } = await prepare(fetchMock);
    // Another tab already installed exactly this version: there is no button left to press again.
    deviceFirmware = '1.1.0';

    await install();

    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('up_to_date');
    expect(notice.value).toBeNull();
  });

  it('CR-016: a recheck that failed in conflict is announced, and a later check clears the note', async () => {
    const fetchMock = manifestThen(binaryResponse([image()]));
    const { install, check, recheck, phase, notice } = await prepare(fetchMock);
    updateOutcome = 409;

    await install();
    expect(phase.value).toBe('conflict');

    deviceOffline = true; // the device is rebooting and does not answer /info yet
    await recheck();
    expect(notice.value).toBe('recheck_failed');
    expect(phase.value).toBe('conflict');

    // Leaving the page and coming back calls check(), which does nothing else in `conflict`: with
    // no reset there the warning would stay up for good, with nothing retrying behind it.
    await check();
    expect(notice.value).toBeNull();
    expect(phase.value).toBe('conflict');
  });

  it('CR-017: a declared size over the cap is refused before the body is read', async () => {
    const oversized = oversizedResponse(FW_DOWNLOAD_MAX_BYTES + 1);
    const fetchMock = manifestThen(oversized.response);
    const { install, phase, failure, message } = await prepare(fetchMock);

    await install();

    expect(oversized.read).not.toHaveBeenCalled();
    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('download');
    expect(message.value).toContain(String(FW_DOWNLOAD_MAX_BYTES));
  });

  it('CR-017: a body that declares no size is refused once it grows past the cap', async () => {
    // Every chunk opens with the ESP magic byte, so the size is the only thing that can stop this
    // download — a refusal for any other reason would mean the running total is not being watched.
    const megabyte = () => image(0xe9, 1024 * 1024);
    const fetchMock = manifestThen(binaryResponse([megabyte(), megabyte(), megabyte()], { contentLength: null }));
    const { install, phase, failure, message } = await prepare(fetchMock);

    await install();

    expect(uploadedSizes).toHaveLength(0);
    expect(phase.value).toBe('failed');
    expect(failure.value).toBe('download');
    expect(message.value).toContain(`larger than ${FW_DOWNLOAD_MAX_BYTES}`);
  });
});
