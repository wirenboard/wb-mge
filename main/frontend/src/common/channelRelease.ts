import { computed, ref } from 'vue';
import { DeviceUpdateError, useFirmware } from '@/common/firmware';
import {
  MANIFEST_URL,
  ManifestError,
  describeChannels,
  parseSignatureBlock,
} from '@/common/firmwareChannels';
import type { ChannelsView, ReleaseLookup } from '@/common/firmwareChannels';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import type { UpdateChannel, Uptime } from '@/common/types';
import { isReconnecting } from '@/common/uptime';
import { api } from '@/utils/api';

// 148 079 bytes of manifest over 40 s is ~30 kbit/s — a weaker link than the one the firmware
// download itself needs, so this never refuses a check on a connection that could still update.
// A timeout is needed at all because the typical "no internet" case is a black hole, not an RST.
export const MANIFEST_TIMEOUT_MS = 40000;
// Abort the download when no byte arrives for this long. A stall timer, not a total duration:
// a slow but alive link must be allowed to finish.
export const FW_DOWNLOAD_STALL_MS = 30000;
// Upper bound on the whole download: 1 265 872 bytes over 300 s is ~34 kbit/s.
export const FW_DOWNLOAD_HARD_CAP_MS = 300000;
// Pause before the first /uptime poll after the 200. The 200 leaves the device before the reboot
// is even scheduled, so polling earlier only samples the old firmware.
export const REBOOT_SETTLE_MS = 4000;
// How long the device is given to come back. A cold start takes seconds; 90 s is headroom for a
// Wi-Fi client reconnect. Waiting longer is less honest than saying "it did not come back".
export const VERIFY_BUDGET_MS = 90000;
export const VERIFY_POLL_MS = 2000;
// Download progress is written to the UI at most this often.
export const PROGRESS_THROTTLE_MS = 100;
// How long the "updated to X" message stays before the page is reloaded into the new interface.
export const VERIFIED_RELOAD_MS = 2000;

// First byte of an ESP application image. The device checks it too, but only after esp_ota_begin()
// has already erased the target partition, so rejecting garbage in the browser is much cheaper.
const ESP_IMAGE_MAGIC = 0xe9;

export type UpdatePhase =
  | 'idle' | 'checking' | 'available' | 'up_to_date' | 'unavailable'
  | 'downloading' | 'uploading' | 'rebooting'
  | 'verified' | 'not_applied' | 'failed' | 'conflict';

// What exactly failed, so the UI can pick the right sentence without parsing `message`.
export type FailureKind = 'download' | 'upload' | 'no_response' | null;

// Module scope: this state survives navigation between views. App.vue renders RouterView without
// KeepAlive, so leaving the System page destroys the component while the download keeps running.
const manifestText = ref<string | null>(null); // raw body, fetched once per SPA session
const release = ref<ReleaseLookup | null>(null);
const channels = ref<ChannelsView | null>(null);
// The channel the current offer was computed for — the value the device confirmed, never the
// local v-model of the selector.
const resolvedChannel = ref<UpdateChannel>('stable');
const phase = ref<UpdatePhase>('idle');
const progress = ref(0); // download percent
const failure = ref<FailureKind>(null);
const message = ref<string | null>(null); // failure detail for the UI (device text, HTTP status, …)

// A check already in flight: two views mounting at once must not fetch the manifest twice.
let checkInFlight: Promise<void> | null = null;

// Phases during which an update operation owns the device: no re-check, no second install.
const BUSY_PHASES: UpdatePhase[] = ['downloading', 'uploading', 'rebooting'];

// Phases an install may be started from. `failed` is included on purpose: a failed attempt leaves
// the button active, unlike `conflict`, where the device is already holding a written image.
const STARTABLE_PHASES: UpdatePhase[] = ['available', 'failed'];

const delay = (ms: number) => new Promise<void>((resolve) => setTimeout(resolve, ms));

// A rejection that can be re-armed on every chunk. Checking the clock inside the loop body does not
// work for a stall: the read promise never settles, so the loop body never runs.
const rejectAfter = (ms: number, reason: string) => {
  let id: ReturnType<typeof setTimeout>;
  const promise = new Promise<never>((_, reject) => {
    id = setTimeout(() => reject(new Error(reason)), ms);
  });
  return { promise, cancel: () => clearTimeout(id) };
};

const joinChunks = (chunks: Uint8Array[], total: number): Uint8Array => {
  const bytes = new Uint8Array(total);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.length;
  }
  return bytes;
};

const fetchManifest = async (): Promise<string> => {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), MANIFEST_TIMEOUT_MS);
  try {
    // No cache busting and no cache: 'no-store' — the object has an ETag and answers 304, so a
    // repeated load costs nothing.
    const response = await fetch(MANIFEST_URL, { signal: controller.signal });
    if (!response.ok) {
      throw new Error(`manifest request failed: HTTP ${response.status}`);
    }
    return await response.text();
  } finally {
    clearTimeout(timer);
  }
};

export const useChannelRelease = () => {
  const { info, fetchInfo } = useInfo();
  const { data: settings, partialRefresh } = useSettings();
  const { update } = useFirmware();

  const signatureOf = (): string => info.value?.signature ?? '';
  const channelOf = (): UpdateChannel => settings.value?.update_channel ?? 'stable';

  const isBusy = computed(() => BUSY_PHASES.includes(phase.value));

  // The update button is offered in `available` and again after a failed attempt, never in
  // `conflict` — there the device already has an image written and is waiting to boot it.
  const canInstall = computed(() =>
    STARTABLE_PHASES.includes(phase.value) && release.value?.ok === true
  );

  // Versions are compared as whole strings. Normalising to x.y.z would collapse 1.0.0-rc29 and
  // 1.0.0 into one, and those are different firmwares — an rc device would stop seeing the release.
  const installedVersion = computed(() => info.value?.firmware ?? '');

  const applyManifest = (text: string) => {
    const signature = signatureOf();
    const channel = channelOf();
    const block = parseSignatureBlock(text, signature); // throws ManifestError on a broken manifest
    resolvedChannel.value = channel;
    if (block === null) {
      console.warn(`[firmware] no update channels published for signature ${signature}`);
      channels.value = null;
      release.value = { ok: false, reason: 'no-signature' };
      phase.value = 'unavailable';
      return;
    }
    const view = describeChannels(block);
    channels.value = view;
    const channelInfo = view[channel];
    if (channelInfo === null) {
      release.value = { ok: false, reason: 'unavailable' };
      phase.value = 'unavailable';
      return;
    }
    release.value = { ok: true, version: channelInfo.version, url: channelInfo.url };
    phase.value = channelInfo.version === installedVersion.value ? 'up_to_date' : 'available';
  };

  const markUnavailable = () => {
    channels.value = null;
    release.value = { ok: false, reason: 'unavailable' };
    phase.value = 'unavailable';
  };

  const runCheck = async () => {
    const signature = signatureOf();
    failure.value = null;
    message.value = null;
    phase.value = 'checking';

    if (!signature) {
      // A device that does not report its signature is a failed check, not a board without
      // published channels — the two get different texts in the UI.
      console.warn('[firmware] the device reports an empty signature, cannot check for updates');
      markUnavailable();
      return;
    }

    if (manifestText.value === null) {
      try {
        manifestText.value = await fetchManifest();
      } catch (err) {
        console.error('[firmware] could not download the release manifest', err);
        markUnavailable();
        return;
      }
    }

    try {
      applyManifest(manifestText.value);
    } catch (err) {
      console.error('[firmware] could not read the release manifest', err);
      if (err instanceof ManifestError) {
        // A broken manifest is not worth keeping: the next check re-downloads it.
        manifestText.value = null;
      }
      markUnavailable();
    }
  };

  /**
   * Recomputes the offer. The manifest is downloaded once per SPA session; switching channels only
   * re-resolves the copy already in memory. Both views call this on mount, and only the first call
   * does any network work.
   */
  const check = async (): Promise<void> => {
    if (isBusy.value || phase.value === 'conflict') {
      return;
    }
    if (checkInFlight) {
      return checkInFlight;
    }
    checkInFlight = runCheck().finally(() => {
      checkInFlight = null;
    });
    return checkInFlight;
  };

  /**
   * Re-reads the device state and recomputes the offer. This is the only way out of `conflict`:
   * the device reboots on its own, and polling it in that state would not make it any faster.
   */
  const recheck = async (): Promise<void> => {
    if (isBusy.value) {
      return;
    }
    try {
      await Promise.all([fetchInfo(), partialRefresh(['update_channel'])]);
    } catch (err) {
      console.error('[firmware] could not re-read the device state', err);
    }
    phase.value = 'idle';
    await check();
  };

  const readUptimeSeconds = async (): Promise<number | null> => {
    try {
      const value = await api<Uptime>('uptime', { timeout: 4000, priority: 'high' });
      return ((value.days * 24 + value.hours) * 60 + value.minutes) * 60 + value.seconds;
    } catch {
      return null;
    }
  };

  const downloadFirmware = async (url: string): Promise<Uint8Array> => {
    const controller = new AbortController();
    const hardCap = setTimeout(() => controller.abort(), FW_DOWNLOAD_HARD_CAP_MS);
    let received = 0;
    try {
      const response = await fetch(url, { signal: controller.signal });
      // fetch does not throw on 4xx, and a missing key is answered with an HTML error page served
      // with CORS — without this check that page would be uploaded to the device as firmware.
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const declared = Number(response.headers.get('Content-Length'));
      const expected = Number.isFinite(declared) && declared > 0 ? declared : 0;
      const chunks: Uint8Array[] = [];
      const reader = response.body?.getReader();
      if (reader) {
        let progressAt = 0;
        for (;;) {
          const stall = rejectAfter(FW_DOWNLOAD_STALL_MS, `no data for ${FW_DOWNLOAD_STALL_MS} ms`);
          const chunk = await Promise.race([reader.read(), stall.promise])
            .finally(() => stall.cancel());
          if (chunk.done) {
            break;
          }
          chunks.push(chunk.value);
          received += chunk.value.length;
          const now = Date.now();
          if (expected > 0 && now - progressAt >= PROGRESS_THROTTLE_MS) {
            progressAt = now;
            progress.value = Math.min(100, Math.round((received / expected) * 100));
          }
        }
      } else {
        const whole = new Uint8Array(await response.arrayBuffer());
        chunks.push(whole);
        received = whole.length;
      }
      if (expected > 0 && received !== expected) {
        throw new Error(`size mismatch: got ${received} of ${expected} bytes`);
      }
      const bytes = joinChunks(chunks, received);
      if (bytes[0] !== ESP_IMAGE_MAGIC) {
        throw new Error(`not an ESP image: first byte 0x${(bytes[0] ?? 0).toString(16)}`);
      }
      progress.value = 100;
      return bytes;
    } catch (err) {
      controller.abort();
      throw err;
    } finally {
      clearTimeout(hardCap);
    }
  };

  /**
   * Waits for the device to come back and reports whether the target version is the one running.
   * "The device rebooted" means uptime went backwards — not that it answered: the 200 for the
   * upload is sent about a second before the reboot is actually performed.
   */
  const waitForReboot = async (uptimeBefore: number | null, wanted: string) => {
    await delay(REBOOT_SETTLE_MS);
    const deadline = Date.now() + VERIFY_BUDGET_MS;
    let rebooted = false;
    while (Date.now() < deadline) {
      const seconds = await readUptimeSeconds();
      if (seconds !== null && (uptimeBefore === null || seconds < uptimeBefore)) {
        rebooted = true;
        break;
      }
      await delay(VERIFY_POLL_MS);
    }
    if (!rebooted) {
      return 'no_response' as const;
    }
    // The version check happens BEFORE any page reload: the SPA is served by the device itself, and
    // a reload issued too early is answered by the still-running old firmware.
    for (let attempt = 0; attempt < 2; attempt += 1) {
      try {
        await fetchInfo();
        break;
      } catch (err) {
        if (attempt === 1) {
          console.error('[firmware] the device came back but /info is not answering', err);
          return 'no_response' as const;
        }
        await delay(VERIFY_POLL_MS);
      }
    }
    const running = info.value?.firmware ?? '';
    if (running === wanted) {
      return 'verified' as const;
    }
    console.error(`[firmware] update not applied: expected ${wanted}, the device runs ${running}`);
    return 'not_applied' as const;
  };

  /**
   * Downloads the firmware of the selected channel and uploads it to the device.
   * The caller is responsible for the confirmation dialog.
   */
  const install = async (): Promise<void> => {
    const offered = release.value;
    if (!STARTABLE_PHASES.includes(phase.value) || !offered || !offered.ok) {
      return;
    }
    const wanted = { version: offered.version, url: offered.url };
    failure.value = null;
    message.value = null;

    // Between the check and the click the device could have been updated from another tab, or the
    // channel could have been switched there. Neither the per-tab button lock nor the device-side
    // guard (which is clean again after a reboot) would catch that.
    try {
      await Promise.all([fetchInfo(), partialRefresh(['update_channel'])]);
      applyManifest(manifestText.value ?? '');
    } catch (err) {
      console.error('[firmware] could not re-read the device state before the update', err);
      markUnavailable();
      return;
    }
    const current = release.value;
    if (phase.value !== 'available' || !current || !current.ok
      || current.version !== wanted.version || current.url !== wanted.url) {
      console.warn('[firmware] the offer changed between the check and the click, not installing');
      return;
    }

    phase.value = 'downloading';
    progress.value = 0;
    let bytes: Uint8Array;
    try {
      bytes = await downloadFirmware(wanted.url);
    } catch (err) {
      console.error(`[firmware] download of ${wanted.url} failed`, err);
      failure.value = 'download';
      message.value = (err as Error)?.message ?? null;
      phase.value = 'failed';
      return;
    }

    // Read the uptime before the upload: the reboot is detected as a drop in this value.
    const uptimeBefore = await readUptimeSeconds();

    phase.value = 'uploading';
    try {
      await update(new Blob([bytes], { type: 'application/octet-stream' }));
    } catch (err) {
      if ((err as Error)?.message === 'update_in_progress') {
        // The device already has an image written and is waiting to boot it. The button stays
        // locked; the only way out is the explicit "check state" button.
        phase.value = 'conflict';
        return;
      }
      console.error('[firmware] the device refused the upload', err);
      failure.value = 'upload';
      message.value = err instanceof DeviceUpdateError ? err.message : null;
      phase.value = 'failed';
      return;
    }

    phase.value = 'rebooting';
    isReconnecting.value = true;
    const verdict = await waitForReboot(uptimeBefore, wanted.version);
    isReconnecting.value = false;
    if (verdict === 'verified') {
      message.value = wanted.version;
      phase.value = 'verified';
      setTimeout(() => {
        if (typeof window !== 'undefined') {
          window.location.reload();
        }
      }, VERIFIED_RELOAD_MS);
      return;
    }
    if (verdict === 'not_applied') {
      message.value = wanted.version;
      phase.value = 'not_applied';
      return;
    }
    failure.value = 'no_response';
    phase.value = 'failed';
  };

  return {
    phase,
    progress,
    failure,
    message,
    release,
    channels,
    resolvedChannel,
    isBusy,
    canInstall,
    check,
    recheck,
    install,
  };
};

// Test-only reset of the module-level state. Production code never calls it: the state is meant to
// outlive every component on the page.
export const __resetChannelReleaseState = () => {
  manifestText.value = null;
  release.value = null;
  channels.value = null;
  resolvedChannel.value = 'stable';
  phase.value = 'idle';
  progress.value = 0;
  failure.value = null;
  message.value = null;
  checkInFlight = null;
};
