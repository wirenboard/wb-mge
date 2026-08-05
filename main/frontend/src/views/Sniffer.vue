<script setup lang="ts">
import { ref, shallowRef, triggerRef, computed, watch, onMounted, onUnmounted, nextTick } from 'vue';
import { useI18n } from 'vue-i18n';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import PacketDecoder from '@/components/PacketDecoder.vue';
import PacketSenderPopup from '@/components/PacketSenderPopup.vue';
import CheckmarkIcon from '@/assets/checkmarkIcon.svg?component';
import { useSettings } from '@/common/settings';
import { useInfo } from '@/common/info';
import { api } from '@/utils/api';
import { downloadFile, exportFileName } from '@/utils/downloadFile';
import { sendPacketToPort } from '@/utils/modbusUtils';
import {
  type SniffRow,
  type ByteRole,
  type CaptureReadyOutcome,
  FC_NAMES,
  SLAVE_NAMES,
  parsePacket,
  updateWallOffsetMs,
  toggleSet,
  computeVirtualWindow,
  trimToCap,
  rowMatchesFilter,
  getRowBytes,
  getRowByteRoles,
} from '@/utils/snifferUtils';

const { t } = useI18n();

// Ring-buffer cap. Optional prop so tests can inject a small cap to exercise the overflow +
// scroll-compensation branch in flushPending(); vue-router passes no prop in production, so the
// default of 50000 preserves the original behavior exactly.
const props = withDefaults(defineProps<{ maxRows?: number }>(), { maxRows: 50000 });

const { data: settings, refresh: refreshSettings } = useSettings();
const { info, fetchInfo } = useInfo();
const senderOpen = ref(false);

const txDisabledForCurrentPort = computed(() => {
  if (portFilter.value === '1') return settings.value?.rs485_1?.tx_disabled ?? false;
  return settings.value?.rs485_2?.tx_disabled ?? false;
});

const rows = shallowRef<SniffRow[]>([]);
const running = ref(false);
const ws = ref<WebSocket | null>(null);
// Timer handle for the WS reconnect delay — stored so it can be cleared in stopCapture().
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
let lastTimestampUs = 0;
// Wall-clock<->device-uptime offset (ms vs Unix epoch); null means "re-anchor on next packet".
let wallOffsetMs: number | null = null;
const wsStatus = ref<'connected' | 'disconnected' | 'reconnecting'>('disconnected');

// Virtualization / ring buffer constants and state. The ring-buffer cap comes from props.maxRows
// (default 50000) so the overflow branch is test-injectable without changing production behavior.
const ROW_HEIGHT_FALLBACK = 29; // fallback row height in px until a real row is measured
const OVERSCAN = 10; // extra rows rendered above/below the viewport
const rowHeight = ref(ROW_HEIGHT_FALLBACK); // measured at runtime
const scrollTop = ref(0);
const viewportH = ref(0);
const autoScroll = ref(true); // follow-tail flag
let resizeObserver: ResizeObserver | null = null;

const tableWrap = ref<HTMLElement | null>(null);
const selected = ref<number | null>(null);
const portFilter = ref('1');
const portOptions = ['1', '2'];
const selectedSlaves = ref<Set<string>>(new Set());
const selectedFcs = ref<Set<string>>(new Set());
const hideErrors = ref(false);

// CRC-error count, maintained incrementally on ingest/trim instead of re-scanning the
// whole rows array each frame. Reset in clearLogs(); adjusted in flushPending().
const errorCount = ref(0);

function getWsUrl(): string {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  return `${proto}://${location.host}/sniffer/ws`;
}

function sendPortStart(port: number) {
  ws.value?.send(JSON.stringify({ cmd: 'start', port }));
}

function sendPortStop(port: number) {
  ws.value?.send(JSON.stringify({ cmd: 'stop', port }));
}

// Scroll handler — rAF-throttled so rapid scroll events do not spam reactive updates.
let scrollRaf: number | null = null;
function onScroll() {
  if (scrollRaf !== null) return;
  scrollRaf = requestAnimationFrame(() => {
    scrollRaf = null;
    const el = tableWrap.value;
    if (!el) return;
    scrollTop.value = el.scrollTop;
    viewportH.value = el.clientHeight;
    if (!rowMeasured) measureRowHeight();
    // Follow-tail: re-enable auto-scroll only when the user is within ~2 rows of the bottom.
    const distanceFromBottom = el.scrollHeight - el.scrollTop - el.clientHeight;
    autoScroll.value = distanceFromBottom <= rowHeight.value * 2;
  });
}

// Row height measurement: border-collapse makes a CSS-fixed height unreliable, so measure a
// real data row once. All data rows are identical height (single line, nowrap, same font),
// so measuring one is enough.
let rowMeasured = false;
function measureRowHeight() {
  const el = tableWrap.value?.querySelector('tr.sniff-row') as HTMLElement | null;
  if (el && el.offsetHeight > 0) {
    rowHeight.value = el.offsetHeight;
    rowMeasured = true;
  }
}

// Batched ingestion: parsing stays in onmessage, but the reactive array mutation is
// coalesced into a single rAF flush so high packet rates do not thrash Vue's reactivity.
let pending: SniffRow[] = [];
let flushRaf: number | null = null;
function scheduleFlush() {
  if (flushRaf !== null) return;
  flushRaf = requestAnimationFrame(flushPending);
}
function flushPending() {
  flushRaf = null;
  if (pending.length === 0) return;
  // Append buffered rows; track CRC-error rows added so errorCount stays accurate.
  let addedErrors = 0;
  for (const r of pending) {
    rows.value.push(r);
    if (r.crc === 'ERR') addedErrors++;
  }
  pending = [];
  // Ring buffer: drop the oldest rows once the cap is exceeded; trimToCap returns the
  // dropped rows so we can subtract any CRC-error rows among them from the incremental count.
  const dropped = trimToCap(rows.value, props.maxRows);
  const removedErrors = dropped.reduce((n, r) => (r.crc === 'ERR' ? n + 1 : n), 0);
  errorCount.value += addedErrors - removedErrors;
  // shallowRef: in-place mutations are invisible to reactivity — notify dependents.
  triggerRef(rows);
  if (dropped.length > 0 && !autoScroll.value) {
    // When the user has scrolled up (not following the tail), dropping the oldest rows shifts
    // the rendered content up. The rendered viewport is built from filteredRows, so the scroll
    // must shift by the number of dropped rows that PASS the current filter — NOT the raw count
    // of dropped rows (which spans both ports and pre-filter data) — otherwise the anchor jumps.
    const filter = {
      port: parseInt(portFilter.value),
      hideErrors: hideErrors.value,
      selectedSlaves: selectedSlaves.value,
      selectedFcs: selectedFcs.value,
    };
    const droppedVisible = dropped.reduce((n, r) => (rowMatchesFilter(r, filter) ? n + 1 : n), 0);
    if (droppedVisible > 0) {
      nextTick(() => {
        const el = tableWrap.value;
        if (el) {
          el.scrollTop = Math.max(0, el.scrollTop - droppedVisible * rowHeight.value);
          scrollTop.value = el.scrollTop;
        }
      });
    }
  }
  if (!rowMeasured) nextTick(measureRowHeight);
  if (autoScroll.value) {
    nextTick(() => {
      const el = tableWrap.value;
      if (el) el.scrollTop = el.scrollHeight;
    });
  }
}

function connectWs() {
  lastTimestampUs = 0;
  // Re-anchor the wall-clock offset on every Start / WS (re)connect (immune to reboots/RTC drift).
  wallOffsetMs = null;
  ws.value = new WebSocket(getWsUrl());
  ws.value.onopen = () => {
    wsStatus.value = 'connected';
    sendPortStart(parseInt(portFilter.value));
    // The socket is open and the per-port `start` command has been written to it — that is what
    // a waiting send is waiting for; see ensureCaptureRunning() for what it does and does not
    // guarantee. Position within this handler does not matter: resolve() only schedules a
    // microtask, so the rest of onopen always runs before any waiting send resumes.
    resolveCaptureReady(true);
  };
  ws.value.onmessage = (ev) => {
    try {
      const msg = JSON.parse(ev.data as string);
      // A sniffer packet without a numeric timestamp is malformed; skip it so it
      // cannot render a NaN / 1970-anchored Time cell with an unset offset.
      if (typeof msg.timestamp_us !== 'number') return;
      // Device rebooted mid-session (uptime went backwards) -> re-anchor.
      if (msg.timestamp_us < lastTimestampUs) wallOffsetMs = null;
      wallOffsetMs = updateWallOffsetMs(wallOffsetMs, msg.timestamp_us, Date.now());
      const { row, timestamp } = parsePacket(msg, lastTimestampUs, wallOffsetMs);
      if (row) {
        lastTimestampUs = timestamp;
        pending.push(row);
        scheduleFlush();
      }
    } catch (e) {
      console.warn('sniffer: failed to parse WS message', e);
    }
  };
  ws.value.onclose = () => {
    ws.value = null;
    if (running.value) {
      wsStatus.value = 'reconnecting';
      // Capture the timer handle so it can be cleared in stopCapture() / onUnmounted().
      reconnectTimer = setTimeout(connectWs, 2000);
    } else {
      wsStatus.value = 'disconnected';
    }
  };
}

/** Delay after switching port mode to allow firmware to complete serial port reinit. */
const PORT_MODE_SWITCH_DELAY_MS = 500;

/** Give up waiting for the capture WebSocket after this long and transmit anyway (see
 *  ensureCaptureRunning) — a send must never hang because the socket cannot be established. */
const CAPTURE_READY_TIMEOUT_MS = 5000;

// Resolvers for callers waiting until the capture is established (see ensureCaptureRunning() for
// what that means). Settled with true in ws.onopen (socket open + `start` written) and with false
// in stopCapture(), so a pending send is never stranded when the user stops the capture.
let captureReadyWaiters: ((ready: boolean) => void)[] = [];

function resolveCaptureReady(ready: boolean) {
  const waiters = captureReadyWaiters;
  captureReadyWaiters = [];
  for (const resolve of waiters) resolve(ready);
}

function waitForCaptureReady(): Promise<boolean> {
  if (running.value && wsStatus.value === 'connected') return Promise.resolve(true);
  return new Promise<boolean>((resolve) => {
    let timer: ReturnType<typeof setTimeout> | null = null;
    const waiter = (ready: boolean) => {
      if (timer !== null) {
        clearTimeout(timer); timer = null;
      }
      resolve(ready);
    };
    captureReadyWaiters.push(waiter);
    timer = setTimeout(() => {
      captureReadyWaiters = captureReadyWaiters.filter(w => w !== waiter);
      resolve(false);
    }, CAPTURE_READY_TIMEOUT_MS);
  });
}

/**
 * Make sure the capture is established before a frame is transmitted, so both the request and the
 * reply land in the list instead of being sent into a socket that is not open yet and lost.
 *
 * What "established" is worth, exactly: the `{"cmd":"start"}` frame was written into an OPEN
 * WebSocket before the send request went out. That is strictly stronger than awaiting
 * startCapture() alone, which returns as soon as connectWs() has *requested* the socket
 * (`new WebSocket(...)` returns immediately) — a frame sent at that point still races the
 * connection. It is NOT proof that the firmware has processed the `start`: the capture command
 * travels on the WebSocket and the frame on a separate POST /ports/N/send, and nothing on this
 * side orders the two.
 *
 * Never rejects. Bringing the capture up is best effort — a send must degrade to "transmit with
 * nothing recording it", never to "do not transmit" (the same principle as
 * CAPTURE_READY_TIMEOUT_MS).
 *
 * @returns 'already-running' when the capture was already established, so there is nothing to
 *   announce; 'started' when THIS call started it and it is now established; 'not-established'
 *   when the frame is about to go out unrecorded — the user pressed Stop, the socket did not
 *   open within CAPTURE_READY_TIMEOUT_MS, or starting the capture failed outright.
 */
async function ensureCaptureRunning(): Promise<CaptureReadyOutcome> {
  if (running.value && wsStatus.value === 'connected') return 'already-running';
  const autoStarted = !running.value;
  if (autoStarted) {
    try {
      await startCapture();
    } catch (e) {
      // connectWs() constructs the WebSocket, and `new WebSocket(url)` throws synchronously on a
      // malformed URL or on mixed content. Roll the UI back out of the running state it never
      // actually reached, then let the caller transmit anyway.
      console.warn('sniffer: could not start the capture before a send', e);
      stopCapture();
      return 'not-established';
    }
    // The user may have hit Stop while startCapture() was in flight — do not block on a
    // connection that will never come.
    if (!running.value) return 'not-established';
  }
  // The result decides what the caller tells the user: false means the capture never came up
  // (Stop, or the timeout), so claiming it was started would be a lie.
  const established = await waitForCaptureReady();
  if (!established) return 'not-established';
  return autoStarted ? 'started' : 'already-running';
}

async function startCapture() {
  // Set running immediately to prevent concurrent calls while async steps execute.
  // The button switches to "Stop" at once, so a second click calls stopCapture() instead.
  running.value = true;
  // Keep previously captured packets and append new ones (no clear on start). Resume
  // following the tail so incoming packets stream into view; the manual "Clear" button
  // remains the way to reset the buffer.
  autoScroll.value = true;

  // Fetch fresh port mode info before deciding whether to switch — the cached
  // info.value may be stale (polled every 5 s) or undefined (not yet loaded).
  try {
    await fetchInfo();
  } catch {
    // If the fetch fails, proceed with whatever is cached (graceful degradation)
  }

  // Guard: user may have clicked Stop while fetchInfo was in flight
  if (!running.value) return;

  if (info.value !== undefined) {
    const portNum = parseInt(portFilter.value);
    // Map port number to the corresponding rs485 key in the Info object
    const rsKeyMap: Partial<Record<number, 'rs485_1' | 'rs485_2'>> = { 1: 'rs485_1', 2: 'rs485_2' };
    const rsKey = rsKeyMap[portNum];
    if (rsKey !== undefined) {
      const currentMode = info.value[rsKey]?.port_mode;
      // The live sniffer is a display overlay over the WebSocket; it only needs the
      // serial port to be open. 'tcp_bridge' and 'passive' already have serial open,
      // so do NOT switch them. Only 'disabled' needs the serial port opened — switch
      // it to the 'passive' transport (serial open, no TCP) before connecting the WS.
      if (currentMode === 'disabled') {
        try {
          await api<void>(`ports/${portNum}/mode`, { method: 'POST', json: { mode: 'passive' } });
        } catch {
          // If the mode switch fails, proceed anyway — WS connect will fail or produce no data
        }
        // Guard: user may have clicked Stop during the api call
        if (!running.value) return;
        // Wait for firmware to complete serial port reinit before connecting
        await new Promise(resolve => setTimeout(resolve, PORT_MODE_SWITCH_DELAY_MS));
        // Guard: user may have clicked Stop during the 500 ms reinit delay
        if (!running.value) return;
        // Refresh info immediately so the sidebar reflects the updated port mode.
        fetchInfo('low').catch(() => {});
      }
    }
  }
  connectWs();
}

function stopCapture() {
  // Cancel the pending flush rAF and flush synchronously so the last buffered packets
  // are not lost when capture stops.
  if (flushRaf !== null) {
    cancelAnimationFrame(flushRaf); flushRaf = null;
  }
  flushPending();
  // Clear any pending reconnect timer before closing the WebSocket.
  if (reconnectTimer !== null) {
    clearTimeout(reconnectTimer); reconnectTimer = null;
  }
  running.value = false;
  wsStatus.value = 'disconnected';
  // Nothing will connect now, so release any send waiting for the capture instead of making
  // it sit out the full CAPTURE_READY_TIMEOUT_MS.
  resolveCaptureReady(false);
  sendPortStop(parseInt(portFilter.value));
  ws.value?.close();
  ws.value = null;
  // Refresh info immediately so the sidebar reflects the updated port mode without
  // waiting for the next polling cycle (which runs every 5 s).
  fetchInfo('low').catch(() => {});
}

watch(portFilter, (newPort, oldPort) => {
  if (!running.value || ws.value === null || wsStatus.value !== 'connected') return;
  sendPortStop(parseInt(oldPort));
  sendPortStart(parseInt(newPort));
});

// Keep the virtual-scroll state consistent when a filter/port change resizes filteredRows.
// No scroll event fires on such changes, so scrollTop.value would otherwise stay stale and
// desync from the DOM (the browser auto-clamps el.scrollTop to the new content height).
// Re-read it on nextTick and recompute autoScroll with the same rule as onScroll().
watch([portFilter, selectedSlaves, selectedFcs, hideErrors], () => {
  nextTick(() => {
    const el = tableWrap.value;
    if (el === null) {
      return;
    }
    scrollTop.value = el.scrollTop;
    const distanceFromBottom = el.scrollHeight - el.scrollTop - el.clientHeight;
    autoScroll.value = distanceFromBottom <= rowHeight.value * 2;
  });
});

function clearLogs() {
  if (flushRaf !== null) {
    cancelAnimationFrame(flushRaf); flushRaf = null;
  }
  pending = [];
  rows.value = [];
  errorCount.value = 0;
  lastTimestampUs = 0;
  // Re-anchor the wall-clock offset on Clear so the next session re-syncs from scratch.
  wallOffsetMs = null;
  scrollTop.value = 0;
  autoScroll.value = true;
  if (tableWrap.value) tableWrap.value.scrollTop = 0;
}

onMounted(() => {
  refreshSettings();
  const el = tableWrap.value;
  if (el) {
    viewportH.value = el.clientHeight;
    // happy-dom (test env) has no ResizeObserver — guard so the integration tests do not throw.
    if (typeof ResizeObserver !== 'undefined') {
      resizeObserver = new ResizeObserver(() => {
        if (tableWrap.value) viewportH.value = tableWrap.value.clientHeight;
        measureRowHeight();
      });
      resizeObserver.observe(el);
    }
  }
});
onUnmounted(() => {
  stopCapture();
  resizeObserver?.disconnect();
  resizeObserver = null;
  if (scrollRaf !== null) {
    cancelAnimationFrame(scrollRaf); scrollRaf = null;
  }
});

// Map a byte role to a scoped BEM class. The actual (static) colors/padding live in CSS
// under `.byte-role*` so no inline static styles are emitted here.
function byteRoleClass(role: ByteRole): string {
  switch (role) {
    case 'address': return 'byte-roleAddress';
    case 'fc': return 'byte-roleFc';
    case 'subcommand': return 'byte-roleSubcommand';
    case 'serial': return 'byte-roleSerial';
    case 'crc': return 'byte-roleCrc';
    case 'data': return 'byte-roleData';
    case 'arbitration': return 'byte-roleArbitration';
    case 'fm-addr': return 'byte-roleFmAddr';
    case 'fm-ext': return 'byte-roleFmExt';
    case 'fm-subcommand': return 'byte-roleFmSubcommand';
    default: return 'byte-roleData';
  }
}

// Rows filtered by port only (for facet stats)
const portRows = computed(() => {
  const p = parseInt(portFilter.value);
  return rows.value.filter(x => x.port === p);
});

// Slave stats for facet rail — arbitration packets have no real slave address, skip them
const slaveStats = computed(() => {
  const counts: Record<string, number> = {};
  for (const r of portRows.value) {
    if (r.isArbitration) continue;
    counts[r.slave] = (counts[r.slave] ?? 0) + 1;
  }
  return counts;
});

const activeSlaves = computed(() =>
  Object.keys(slaveStats.value).sort((a, b) => slaveStats.value[b] - slaveStats.value[a])
);

const maxSlaveCount = computed(() =>
  Math.max(1, ...Object.values(slaveStats.value))
);

// FC stats for facet rail
const fcStats = computed(() => {
  const counts: Record<string, number> = {};
  for (const r of portRows.value) {
    counts[r.fc_code] = (counts[r.fc_code] ?? 0) + 1;
  }
  return counts;
});

const activeFcs = computed(() =>
  Object.keys(fcStats.value).sort((a, b) => fcStats.value[b] - fcStats.value[a])
);

const maxFcCount = computed(() =>
  Math.max(1, ...Object.values(fcStats.value))
);

function fcCodeNum(hexCode: string): number {
  return parseInt(hexCode, 16);
}

const filteredRows = computed(() => {
  // No facet filters active → portRows (already port-scoped and memoized for the facet stats)
  // IS the result. Return it directly to avoid a second full-array scan + allocation each frame.
  if (!hideErrors.value && selectedSlaves.value.size === 0 && selectedFcs.value.size === 0) {
    return portRows.value;
  }
  // Facet filters active → filter the smaller, port-scoped portRows (not the full rows.value),
  // reusing the same rowMatchesFilter predicate as the ring-buffer scroll compensation so the
  // two stay in lockstep.
  const filter = {
    port: parseInt(portFilter.value),
    hideErrors: hideErrors.value,
    selectedSlaves: selectedSlaves.value,
    selectedFcs: selectedFcs.value,
  };
  return portRows.value.filter(x => rowMatchesFilter(x, filter));
});

// Virtualization: render only the visible window of rows. We intentionally do NOT subtract
// the sticky thead height from the index math — the OVERSCAN (10 rows ≈ 290px) absorbs the
// ~35px header overlap, so there is never a blank gap.
const virtualWindow = computed(() =>
  computeVirtualWindow(scrollTop.value, viewportH.value, rowHeight.value, filteredRows.value.length, OVERSCAN),
);
const visibleRows = computed(() =>
  filteredRows.value.slice(virtualWindow.value.startIndex, virtualWindow.value.endIndex),
);
// Decode per-byte hex + semantic roles lazily, ONLY for rows in the visible virtual-scroll
// window. These arrays are intentionally not stored on every SniffRow (huge memory cost);
// ~visible rows are cheap to recompute each frame. Field names avoid clashing with the
// existing numeric `bytes` (byte count) on SniffRow.
const visibleCells = computed(() =>
  visibleRows.value.map(r => ({
    ...r,
    hexBytes: getRowBytes(r.pl),
    roles: getRowByteRoles(r.pl, r.direction),
  })),
);
const padTop = computed(() => virtualWindow.value.padTop);
const padBottom = computed(() => virtualWindow.value.padBottom);

const sel = computed(() =>
  selected.value !== null ? filteredRows.value.find(r => r.id === selected.value) ?? null : null
);

// Resend: re-inject the selected packet's raw RTU frame on the current port. The captured
// frame (sel.pl) already carries a valid CRC, so it is sent byte-for-byte. Only frames with a
// valid CRC are resendable; arbitration frames (crc 'N/A'), timeouts (empty payload) and
// CRC-error frames are not real/trustworthy frames and must not be replayed.
const resending = ref(false);
const resendMsg = ref('');
const resendIsError = ref(false);
// What the last resend had to do about the capture, so the user is told what actually happened
// instead of the UI changing state behind their back (or claiming a capture that never came up).
// null = no resend yet / nothing to announce.
const resendCaptureNotice = ref<CaptureReadyOutcome | null>(null);

const canResend = computed(() =>
  sel.value !== null && !sel.value.isArbitration && sel.value.pl.length > 0 && sel.value.crc === 'OK'
);

const resendDisabled = computed(() =>
  !canResend.value || txDisabledForCurrentPort.value || resending.value
);

const resendDisabledReason = computed(() => {
  const row = sel.value;
  if (row === null) return '';
  if (txDisabledForCurrentPort.value) return t('resend_tx_disabled');
  // A real frame (non-arbitration, non-empty) with a bad/absent CRC must not be replayed.
  if (!row.isArbitration && row.pl.length > 0 && row.crc !== 'OK') return t('resend_crc_err');
  if (!canResend.value) return t('resend_unavailable');
  return '';
});

async function resendSelected() {
  const row = sel.value;
  if (row === null || !canResend.value) return;
  // Pin the destination port together with the row. The port buttons stay live while the send
  // waits for the capture (up to CAPTURE_READY_TIMEOUT_MS), and a frame captured on port 1 must
  // not be replayed onto port 2 — a different bus with different devices — just because the
  // filter moved in the meantime.
  const port = portFilter.value;
  resending.value = true;
  resendMsg.value = '';
  resendIsError.value = false;
  resendCaptureNotice.value = null;
  try {
    // Resending has the same blind spot as the packet sender: with the capture stopped neither
    // the replayed frame nor the answer is recorded. Establish it first, then say what actually
    // happened. ensureCaptureRunning() never rejects, so a capture that cannot be started makes
    // the resend go out unrecorded instead of cancelling it.
    resendCaptureNotice.value = await ensureCaptureRunning();
    // Strip the display spaces from the payload to get a compact hex string.
    const hex = row.pl.replace(/\s+/g, '');
    const res = await sendPacketToPort(port, hex);
    resendMsg.value = t('resend_sent', { n: res.sent, port });
  } catch (e: unknown) {
    resendIsError.value = true;
    resendMsg.value = e instanceof Error ? e.message : String(e);
  } finally {
    resending.value = false;
  }
}

// Clear stale resend feedback when the selected row changes.
watch(selected, () => {
  resendMsg.value = '';
  resendIsError.value = false;
  resendCaptureNotice.value = null;
});

function senderPillClass(sender: string) {
  return 'sender-pill sender-' + sender.toLowerCase();
}

function exportCsv() {
  const headers = ['#', 'Time', 'Δt', 'Sender', 'Slave', 'Function code', 'Payload (HEX)', 'Bytes', 'CRC'];
  const csvRows = [headers.join(',')];
  for (const r of filteredRows.value) {
    const row = [
      r.id,
      r.t,
      r.dt,
      r.sender,
      `0x${r.slave}`,
      `"${r.fc}"`,
      `"${r.pl}"`,
      r.bytes,
      r.crc,
    ];
    csvRows.push(row.join(','));
  }
  const blob = new Blob([csvRows.join('\n')], { type: 'text/csv;charset=utf-8;' });
  // Go through downloadFile() rather than a local anchor: it is the one download path that
  // appends the anchor to the DOM, which Firefox requires for the click to do anything.
  downloadFile(exportFileName(`sniffer-port${portFilter.value}`, 'csv'), blob);
}
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')">
      <template #default>
        <div class="sniffer-toolbar">
          <div class="sniffer-toolbar-group toolbar-stats">
            <div class="heading-stats">
              <span><b class="mono">{{ rows.length.toLocaleString() }}</b> {{ t('packets') }}</span>
              <span><b class="mono stat-err">{{ errorCount }}</b> {{ errorCount === 1 ? t('error') : t('errors') }}</span>
              <label v-if="errorCount > 0" class="hide-errors-toggle">
                <input v-model="hideErrors" type="checkbox" />
                {{ t('hide_errors') }}
              </label>
            </div>
          </div>

          <div class="sniffer-toolbar-group toolbar-capture">
            <div class="filter-ports">
              <button
                v-for="p in portOptions" :key="p"
                :class="['port-btn', { active: portFilter === p }]"
                @click="portFilter = p"
              >
{{ t('port_n', { n: p }) }}
</button>
            </div>
            <Button :variant="running ? 'danger' : 'primary'" @click="running ? stopCapture() : startCapture()">
              {{ running ? t('stop') : t('start') }}
            </Button>
          </div>

          <div class="sniffer-toolbar-group toolbar-data">
            <Button variant="outline" :disabled="txDisabledForCurrentPort" @click="senderOpen = true">▶ {{ t('send_packet') }}</Button>
            <Button variant="outline" @click="clearLogs()">{{ t('clear') }}</Button>
            <Button variant="outline" :disabled="filteredRows.length === 0" @click="exportCsv()">{{ t('export_csv') }}</Button>
          </div>
        </div>
      </template>
    </Heading>

    <!-- Main area: facet rail + log table (position: relative for popup anchor) -->
    <div class="sniffer-content-wrap">
    <div class="sniffer-main">
      <!-- Facet rail -->
      <aside class="facet-rail">
        <!-- Slave ID section -->
        <div class="facet-section">
          <div class="facet-section-header">
            <div>
              <div class="facet-section-title">{{ t('facet_slave_id') }}</div>
              <div class="facet-section-hint">{{ activeSlaves.length }} {{ t('seen') }} · {{ selectedSlaves.size || t('all') }} {{ t('selected') }}</div>
            </div>
            <button class="facet-clear" :style="{ visibility: selectedSlaves.size > 0 ? 'visible' : 'hidden' }" @click="selectedSlaves = new Set()">{{ t('clear') }}</button>
          </div>
          <button
            v-for="slave in activeSlaves" :key="slave"
            class="facet-row"
            :data-on="selectedSlaves.has(slave) ? 'true' : 'false'"
            @click="selectedSlaves = toggleSet(selectedSlaves, slave)"
          >
            <span class="facet-check">
              <CheckmarkIcon v-if="selectedSlaves.has(slave)" />
            </span>
            <span class="facet-id-label">
              <span class="mono facet-idMono">{{ slave }}</span>
              <span class="facet-label facet-labelSmall mono muted">
                {{ SLAVE_NAMES[parseInt(slave, 16)] ?? (isNaN(parseInt(slave, 16)) ? slave : `0x${slave} · ${parseInt(slave, 16)}`) }}
              </span>
            </span>
            <span class="facet-count">{{ slaveStats[slave] }}</span>
            <span class="facet-bar"><span :style="{ width: `${(slaveStats[slave] / maxSlaveCount) * 100}%` }" /></span>
          </button>
        </div>

        <!-- Function code section -->
        <div class="facet-section">
          <div class="facet-section-header">
            <div>
              <div class="facet-section-title">{{ t('facet_function_code') }}</div>
              <div class="facet-section-hint">{{ activeFcs.length }} {{ t('seen') }} · {{ selectedFcs.size || t('all') }} {{ t('selected') }}</div>
            </div>
            <button class="facet-clear" :style="{ visibility: selectedFcs.size > 0 ? 'visible' : 'hidden' }" @click="selectedFcs = new Set()">{{ t('clear') }}</button>
          </div>
          <button
            v-for="code in activeFcs" :key="code"
            class="facet-row"
            :data-on="selectedFcs.has(code) ? 'true' : 'false'"
            @click="selectedFcs = toggleSet(selectedFcs, code)"
          >
            <span class="facet-check">
              <CheckmarkIcon v-if="selectedFcs.has(code)" />
            </span>
            <span class="facet-id-label">
              <span class="mono facet-idMono">{{ code }}</span>
              <span class="facet-label">
                {{ FC_NAMES[fcCodeNum(code)] || 'Unknown' }}
              </span>
            </span>
            <span class="facet-count">{{ fcStats[code] }}</span>
            <span class="facet-bar"><span :style="{ width: `${(fcStats[code] / maxFcCount) * 100}%` }" /></span>
          </button>
        </div>
      </aside>

      <!-- Log table -->
      <div class="sniffer-body">
      <div ref="tableWrap" class="sniffer-table-wrap" @scroll="onScroll">
        <table class="sniffer-table">
          <thead>
            <tr>
              <th class="col-id">#</th>
              <th class="col-time" :title="t('col_time')">{{ t('col_time') }}</th>
              <th class="col-dt">&Delta;t</th>
              <th class="col-sender" :title="t('col_sender')">{{ t('col_sender') }}</th>
              <th class="col-slave" :title="t('col_slave')">{{ t('col_slave') }}</th>
              <th class="col-fc" :title="t('col_function')">{{ t('col_function') }}</th>
              <th class="col-payload" :title="t('col_payload')">{{ t('col_payload') }}</th>
              <th class="col-bytes" :title="t('col_bytes')">{{ t('col_bytes') }}</th>
              <th class="col-crc" :title="t('col_crc')">{{ t('col_crc') }}</th>
            </tr>
          </thead>
          <tbody>
            <tr v-if="padTop > 0" class="sniff-spacer" aria-hidden="true"><td :colspan="9" :style="{ height: padTop + 'px' }"></td></tr>
            <tr
              v-for="r in visibleCells" :key="r.id"
              :class="['sniff-row', { selected: selected === r.id, 'err-row': r.crc === 'ERR' && !r.isArbitration }]"
              @click="selected = r.id"
            >
              <td class="mono muted">{{ r.id }}</td>
              <td class="mono">{{ r.t }}</td>
              <td class="mono muted">{{ r.dt }}</td>
              <td><span :class="senderPillClass(r.sender)">{{ r.sender }}</span></td>
              <td class="mono col-slave-cell" :title="r.isArbitration ? undefined : (SLAVE_NAMES[parseInt(r.slave, 16)] ? `0x${r.slave} · ${SLAVE_NAMES[parseInt(r.slave, 16)]}` : `0x${r.slave} (${parseInt(r.slave, 16)})`)">
                <span v-if="r.isArbitration" class="muted">—</span>
                <span v-else>0x{{ r.slave }}</span>
              </td>
              <td :class="['mono', 'fc-cell', { 'fc-cellException': r.isException }]" :title="r.tooltip || undefined">{{ r.fc }}</td>
              <td>
                <span class="hex-payload">
                  <span
                    v-for="(b, i) in r.hexBytes" :key="i"
                    :class="['hex-byte', byteRoleClass(r.roles[i] ?? 'unknown')]"
                  >{{ b }}</span>
                </span>
              </td>
              <td class="mono muted">{{ r.bytes }}</td>
              <td>
                <span v-if="r.crc === 'ERR'" class="crc-err mono">ERR</span>
                <span v-else-if="r.crc === 'N/A'" class="muted mono">—</span>
                <span v-else class="crc-ok mono">OK</span>
              </td>
            </tr>
            <tr v-if="padBottom > 0" class="sniff-spacer" aria-hidden="true"><td :colspan="9" :style="{ height: padBottom + 'px' }"></td></tr>
          </tbody>
        </table>
      </div>

      <!-- Detail panel: resend action bar + decoder -->
      <div v-if="sel" class="pkt-resend-bar">
        <Button variant="outline" :disabled="resendDisabled" :title="resendDisabledReason || undefined" @click="resendSelected()">
          ↻ {{ t('resend') }}
        </Button>
        <span v-if="resendMsg" :class="['pkt-resend-msg', { 'pkt-resend-msgErr': resendIsError }]">{{ resendMsg }}</span>
        <span v-else-if="resendDisabledReason" class="pkt-resend-hint">{{ resendDisabledReason }}</span>
        <span v-if="resendCaptureNotice === 'started'" class="pkt-resend-hint">{{ t('capture_autostarted') }}</span>
        <span v-else-if="resendCaptureNotice === 'not-established'" class="pkt-resend-hint">{{ t('capture_not_running') }}</span>
      </div>
      <PacketDecoder v-if="sel" :packet="sel" />
      </div><!-- /sniffer-body -->
    </div><!-- /sniffer-main -->

    <PacketSenderPopup
      v-if="senderOpen"
      :port-num="portFilter"
      :tx-disabled="txDisabledForCurrentPort"
      :ensure-capture="ensureCaptureRunning"
      @close="senderOpen = false"
    />
    </div><!-- /sniffer-content-wrap -->
  </Layout>
</template>

<style scoped>
.sniffer-toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 8px 20px;
  align-items: center;
  width: 100%;
}

.sniffer-toolbar-group {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.toolbar-stats { margin-right: auto; }

@media (max-width: 560px) {
  .sniffer-toolbar { gap: 8px; }
  .toolbar-stats { margin-right: 0; width: 100%; }
  .sniffer-toolbar-group { width: 100%; }
  .toolbar-capture { justify-content: space-between; }
  .toolbar-data { justify-content: flex-start; }
  .toolbar-capture > button[class*="port"] { flex: 0 0 auto; }
}

/* Heading stats counter */
.heading-stats {
  display: flex;
  gap: 12px;
  font-size: 12px;
  color: var(--text-muted);
  white-space: nowrap;
  align-items: center;
}

.heading-stats b {
  font-weight: 500;
  color: var(--text-color);
}

.stat-err {
  color: var(--mb-err) !important;
}

.hide-errors-toggle {
  display: flex;
  align-items: center;
  gap: 5px;
  cursor: pointer;
  font-size: 12px;
  color: var(--text-muted);
  user-select: none;
}

.hide-errors-toggle input {
  cursor: pointer;
  accent-color: var(--primary-color);
}

/* Port selector in heading */
.filter-ports {
  display: flex;
  gap: 4px;
}

.port-btn {
  height: 32px;
  padding: 0 12px;
  font-size: 13px;
  background: transparent;
  border: 1px solid var(--border-color);
  color: var(--text-secondary);
  border-radius: var(--r-sm);
  cursor: pointer;
  transition: background 0.12s, border-color 0.12s, color 0.12s;
}

.port-btn:hover {
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
}

.port-btn.active {
  background: var(--primary-color);
  border-color: var(--primary-color);
  color: #fff;
}

/* Wrapper providing the positioning context for the floating popup */
.sniffer-content-wrap {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-height: 0;
  position: relative;
}

.sniffer-main {
  flex: 1;
  display: flex;
  min-height: 0;
}

.facet-rail {
  width: 280px;
  flex-shrink: 0;
  background: var(--bg-surface-subtle);
  border-right: 1px solid var(--border-color);
  overflow-y: auto;
  padding: 6px 0 14px;
}

.facet-section {
  padding: 14px 0 4px;
}

.facet-section-header {
  padding: 0 16px 8px;
  display: flex;
  align-items: baseline;
  justify-content: space-between;
}

.facet-section-title {
  font-size: 10.5px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: var(--text-muted);
  font-weight: 600;
}

.facet-section-hint {
  font-size: 10.5px;
  color: var(--text-muted);
  margin-top: 2px;
}

.facet-clear {
  border: 0;
  background: transparent;
  color: var(--text-muted);
  font-size: 11px;
  cursor: pointer;
  padding: 0;
}

.facet-row {
  width: 100%;
  appearance: none;
  border-top: 0;
  border-right: 0;
  border-bottom: 0;
  border-left: 2px solid transparent;
  border-radius: 0;
  outline: none;
  background: transparent;
  display: grid;
  grid-template-columns: 16px 1fr auto 44px;
  align-items: center;
  gap: 10px;
  padding: 6px 16px;
  cursor: pointer;
  font-size: 12px;
  color: var(--text-secondary);
  text-align: left;
}

.facet-row:hover {
  border-top: 0;
  border-right: 0;
  border-bottom: 0;
  background: var(--bg-surface);
  color: var(--text-color);
}

.facet-row:focus-visible {
  outline: none;
}

.facet-row[data-on="true"] {
  background: color-mix(in oklch, var(--primary-color) 6%, white);
  color: var(--text-color);
  border-left-color: var(--primary-color);
}

.facet-row[data-on="true"] .facet-count {
  color: var(--primary-color);
  font-weight: 600;
}

.facet-check {
  width: 14px;
  height: 14px;
  border-radius: 3px;
  border: 1.2px solid var(--border-strong);
  background: #fff;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
}

.facet-row[data-on="true"] .facet-check {
  background: var(--primary-color);
  border-color: var(--primary-color);
}

.facet-id-label {
  display: flex;
  flex-direction: column;
  min-width: 0;
  gap: 1px;
}

.facet-label {
  min-width: 0;
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
  font-size: 12px;
}

.facet-idMono {
  font-weight: 600;
}

.facet-labelSmall {
  font-size: 11px;
}

.col-slave-cell {
  font-weight: 500;
}

.facet-count {
  font-family: var(--font-mono);
  font-size: 11px;
  color: var(--text-muted);
  text-align: right;
}

.facet-bar {
  height: 3px;
  border-radius: 2px;
  background: color-mix(in oklch, var(--border-color) 60%, white);
  overflow: hidden;
}

.facet-bar span {
  display: block;
  height: 100%;
  background: color-mix(in oklch, var(--primary-color) 65%, white);
}

.facet-row[data-on="true"] .facet-bar span {
  background: var(--primary-color);
}

.sniffer-body {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-height: 0;
  background: var(--bg-surface);
}

.sniffer-table-wrap {
  flex: 1;
  overflow: auto;
  /* Right-edge fade hints there is content to scroll horizontally — only when overflowing */
  background:
    linear-gradient(to right, var(--bg-surface) 30%, rgba(255, 255, 255, 0)) left center,
    linear-gradient(to right, rgba(255, 255, 255, 0), var(--bg-surface) 70%) right center,
    radial-gradient(farthest-side at 0 50%, rgba(15, 23, 42, 0.10), rgba(0, 0, 0, 0)) left center,
    radial-gradient(farthest-side at 100% 50%, rgba(15, 23, 42, 0.10), rgba(0, 0, 0, 0)) right center;
  background-repeat: no-repeat;
  background-size: 24px 100%, 24px 100%, 12px 100%, 12px 100%;
  background-attachment: local, local, scroll, scroll;
}

.sniffer-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12.5px;
  table-layout: fixed;
}

.sniffer-table thead {
  position: sticky;
  top: 0;
  z-index: 1;
}

.sniffer-table th {
  background: var(--bg-surface-subtle);
  font-size: 10.5px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-muted);
  font-weight: 500;
  padding: 7px 12px;
  text-align: left;
  border-bottom: 1px solid var(--border-color);
  border-right: none;
  border-left: none;
  white-space: nowrap;
  /* Safety net for table-layout:fixed: clip an over-wide localized header (e.g. "Отправитель")
     instead of letting it overflow into the neighbouring column. */
  overflow: hidden;
  text-overflow: ellipsis;
}

.sniffer-table td {
  padding: 6px 12px;
  border-bottom: 1px solid var(--border-color);
  border-right: none;
  border-left: none;
  white-space: nowrap;
}

.sniffer-table tbody tr {
  cursor: pointer;
  transition: background 0.08s;
}

.sniffer-table tbody tr:hover {
  background: var(--bg-surface-subtle);
}

.sniffer-table tbody tr.selected {
  background: color-mix(in oklch, var(--primary-color) 6%, var(--bg-surface));
}

.sniffer-table tbody tr.err-row {
  background: color-mix(in oklch, var(--mb-err) 4%, var(--bg-surface));
}

.sniffer-table tbody tr.err-row.selected {
  background: color-mix(in oklch, var(--mb-err) 8%, var(--bg-surface));
}

/* Virtualization spacer rows: occupy the height of the off-screen rows above/below the window */
.sniffer-table tbody tr.sniff-spacer {
  cursor: default;
}
.sniffer-table tbody tr.sniff-spacer:hover {
  background: transparent;
}
.sniffer-table tbody tr.sniff-spacer td {
  padding: 0;
  border: 0;
}

.col-id { width: 56px; }
.col-time { width: 100px; }
.col-dt { width: 68px; }
.col-sender { width: 100px; }
.col-slave { width: 54px; }
.col-fc { width: 240px; }
.col-payload { max-width: 0; width: 100%; }
.col-bytes { width: 60px; }
.col-crc { width: 50px; }

.fc-cell {
  font-size: 12px;
  color: var(--text-secondary);
}

/* Exception reply (function byte with bit 7 set): the label already reads "Error: <function>",
   the color makes a rejected reply stand out from a successful one in a dense list. */
.fc-cellException {
  color: var(--mb-err);
  font-weight: 500;
}

.sender-pill {
  display: inline-block;
  font-family: var(--font-mono);
  font-size: 11px;
  font-weight: 600;
  padding: 1px 7px;
  border-radius: 4px;
  border: 1px solid;
  letter-spacing: 0.04em;
}

.sender-master {
  color: var(--mb-master);
  background: color-mix(in oklch, var(--mb-master) 8%, white);
  border-color: color-mix(in oklch, var(--mb-master) 25%, white);
}

.sender-slave {
  color: var(--mb-slave);
  background: color-mix(in oklch, var(--mb-slave) 6%, white);
  border-color: color-mix(in oklch, var(--mb-slave) 22%, white);
}

.sender-err {
  color: var(--mb-err);
  background: color-mix(in oklch, var(--mb-err) 8%, white);
  border-color: color-mix(in oklch, var(--mb-err) 25%, white);
}

.sender-timeout {
  color: var(--text-muted);
  background: color-mix(in oklch, var(--text-muted) 6%, var(--bg-surface));
  border-color: color-mix(in oklch, var(--text-muted) 20%, var(--bg-surface));
}

.hex-payload {
  font-family: var(--font-mono);
  font-size: 12px;
  letter-spacing: 0.02em;
  font-weight: 500;
  display: block;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  max-width: 100%;
}

.hex-byte {
  margin-right: 4px;
  display: inline-block;
}

/* Per-byte semantic role styling (static colors extracted from byteRoleStyle) */
.byte-roleAddress,
.byte-roleSerial {
  color: #fff;
  background: var(--mb-master);
  padding: 1px 4px;
  border-radius: 3px;
}

.byte-roleFc,
.byte-roleSubcommand {
  color: #fff;
  background: var(--mb-hex-slot);
  padding: 1px 4px;
  border-radius: 3px;
}

.byte-roleCrc {
  color: var(--mb-hex-crc);
}

.byte-roleData {
  color: var(--mb-data);
}

.byte-roleArbitration {
  color: var(--text-muted);
  font-weight: 400;
}

/* FM wrapper "not real" fields — same hue but paler background */
.byte-roleFmAddr {
  color: #fff;
  background: color-mix(in oklch, var(--mb-master) 45%, transparent);
  padding: 1px 4px;
  border-radius: 3px;
}

.byte-roleFmExt,
.byte-roleFmSubcommand {
  color: #fff;
  background: color-mix(in oklch, var(--mb-hex-slot) 45%, transparent);
  padding: 1px 4px;
  border-radius: 3px;
}

.crc-err {
  color: var(--mb-err);
  font-weight: 600;
  font-size: 11px;
}

.crc-ok {
  color: var(--mb-ok);
  font-weight: 500;
  font-size: 11px;
}

/* Resend action bar above the packet decoder detail panel */
.pkt-resend-bar {
  display: flex;
  align-items: center;
  gap: 12px;
  flex-shrink: 0;
  padding: 8px 20px;
  border-top: 1px solid var(--border-color);
  background: var(--bg-surface-subtle);
}

.pkt-resend-msg {
  font-size: 12px;
  color: var(--mb-ok);
}

.pkt-resend-msgErr {
  color: var(--mb-err);
}

.pkt-resend-hint {
  font-size: 12px;
  color: var(--text-muted);
  font-style: italic;
}

@media (max-width: 680px) {
  .sniffer-main {
    flex-direction: column;
  }
  .facet-rail {
    width: 100%;
    border-right: 0;
    border-bottom: 1px solid var(--border-color);
    max-height: 220px;
  }
  /* Hide low-priority columns on mobile; horizontal scroll still works for the rest */
  .sniffer-table th.col-id,
  .sniffer-table td:nth-child(1),
  .sniffer-table th.col-dt,
  .sniffer-table td:nth-child(3),
  .sniffer-table th.col-bytes,
  .sniffer-table td:nth-child(8),
  .sniffer-table th.col-crc,
  .sniffer-table td:nth-child(9) {
    display: none;
  }
}
</style>

<i18n>
{
  "en": {
    "title": "Sniffer",
    "crumbs": "Modbus packet capture",
    "start": "Start",
    "stop": "Stop",
    "clear": "Clear",
    "seen": "seen",
    "selected": "selected",
    "all": "all",
    "errors_only": "Errors only",
    "packets": "packets",
    "error": "error",
    "errors": "errors",
    "col_time": "Time",
    "col_sender": "Sender",
    "col_slave": "Slave",
    "col_function": "Function",
    "col_fc": "Function code",
    "col_payload": "Payload",
    "col_bytes": "Bytes",
    "col_crc": "CRC",
    "facet_slave_id": "Slave ID",
    "facet_function_code": "Function code",
    "parsed": "Parsed",
    "start_addr": "Starting address",
    "quantity": "Quantity",
    "size": "Size",
    "raw_bytes": "Raw bytes",
    "export_csv": "Export CSV",
    "hide_errors": "Hide errors",
    "send_packet": "Send packet",
    "resend": "Resend",
    "resend_sent": "Sent {n} bytes to port {port}",
    "resend_tx_disabled": "TX is disabled for this port",
    "resend_unavailable": "This packet cannot be resent",
    "resend_crc_err": "A frame with a CRC error cannot be resent",
    "capture_autostarted": "Capture started automatically",
    "capture_not_running": "Sent without a running capture, so the packets will not appear in the list",
    "port_n": "Port {n}"
  },
  "ru": {
    "title": "Sniffer",
    "crumbs": "Перехват пакетов Modbus",
    "start": "Старт",
    "stop": "Стоп",
    "clear": "Очистить",
    "seen": "видно",
    "selected": "выбрано",
    "all": "все",
    "errors_only": "Только ошибки",
    "packets": "пакетов",
    "error": "ошибка",
    "errors": "ошибок",
    "col_time": "Время",
    "col_sender": "Отправитель",
    "col_slave": "Адрес",
    "col_function": "Функция",
    "col_fc": "Код функции",
    "col_payload": "Данные",
    "col_bytes": "Байт",
    "col_crc": "CRC",
    "facet_slave_id": "Slave ID",
    "facet_function_code": "Код функции",
    "parsed": "Разобрано",
    "start_addr": "Начальный адрес",
    "quantity": "Количество",
    "size": "Размер",
    "raw_bytes": "Сырые байты",
    "export_csv": "Экспорт CSV",
    "hide_errors": "Скрывать ошибки",
    "send_packet": "Отправить пакет",
    "resend": "Отправить повторно",
    "resend_sent": "Отправлено {n} байт на порт {port}",
    "resend_tx_disabled": "TX отключён для этого порта",
    "resend_unavailable": "Этот пакет нельзя отправить повторно",
    "resend_crc_err": "Кадр с ошибкой CRC нельзя отправить повторно",
    "capture_autostarted": "Перехват запущен автоматически",
    "capture_not_running": "Отправлено без активного перехвата, пакеты не попадут в список",
    "port_n": "Порт {n}"
  },
  "kk": {
    "title": "Sniffer",
    "crumbs": "Modbus пакеттерін ұстау",
    "start": "Бастау",
    "stop": "Тоқтату",
    "clear": "Тазалау",
    "seen": "көрінді",
    "selected": "таңдалды",
    "all": "барлығы",
    "errors_only": "Тек қателер",
    "packets": "пакет",
    "error": "қате",
    "errors": "қате",
    "col_time": "Уақыт",
    "col_sender": "Жіберуші",
    "col_slave": "Slave",
    "col_function": "Функция",
    "col_fc": "Функция коды",
    "col_payload": "Payload",
    "col_bytes": "Байт",
    "col_crc": "CRC",
    "facet_slave_id": "Slave ID",
    "facet_function_code": "Функция коды",
    "parsed": "Талданған",
    "start_addr": "Бастапқы адрес",
    "quantity": "Саны",
    "size": "Өлшемі",
    "raw_bytes": "Шикі байттар",
    "export_csv": "CSV жүктеу",
    "hide_errors": "Қателерді жасыру",
    "send_packet": "Пакет жіберу",
    "resend": "Қайта жіберу",
    "resend_sent": "{port} портына {n} байт жіберілді",
    "resend_tx_disabled": "Бұл порт үшін TX өшірілген",
    "resend_unavailable": "Бұл пакетті қайта жіберу мүмкін емес",
    "resend_crc_err": "CRC қатесі бар кадрды қайта жіберу мүмкін емес",
    "capture_autostarted": "Ұстау автоматты түрде іске қосылды",
    "capture_not_running": "Белсенді ұстаусыз жіберілді, пакеттер тізімде көрінбейді",
    "port_n": "Порт {n}"
  },
  "it": {
    "title": "Sniffer",
    "crumbs": "Cattura pacchetti Modbus",
    "start": "Avvia",
    "stop": "Ferma",
    "clear": "Cancella",
    "seen": "visti",
    "selected": "selezionati",
    "all": "tutti",
    "errors_only": "Solo errori",
    "packets": "pacchetti",
    "error": "errore",
    "errors": "errori",
    "col_time": "Ora",
    "col_sender": "Mittente",
    "col_slave": "Slave",
    "col_function": "Funzione",
    "col_fc": "Codice funzione",
    "col_payload": "Payload",
    "col_bytes": "Byte",
    "col_crc": "CRC",
    "facet_slave_id": "Slave ID",
    "facet_function_code": "Codice funzione",
    "parsed": "Analizzato",
    "start_addr": "Indirizzo iniziale",
    "quantity": "Quantità",
    "size": "Dimensione",
    "raw_bytes": "Byte grezzi",
    "export_csv": "Esporta CSV",
    "hide_errors": "Nascondi errori",
    "send_packet": "Invia pacchetto",
    "resend": "Reinvia",
    "resend_sent": "Inviati {n} byte alla porta {port}",
    "resend_tx_disabled": "TX disabilitato per questa porta",
    "resend_unavailable": "Questo pacchetto non può essere reinviato",
    "resend_crc_err": "Un frame con errore CRC non può essere reinviato",
    "capture_autostarted": "Cattura avviata automaticamente",
    "capture_not_running": "Inviato senza cattura attiva, i pacchetti non compariranno nell'elenco",
    "port_n": "Porta {n}"
  },
  "de": {
    "title": "Sniffer",
    "crumbs": "Modbus-Paketerfassung",
    "start": "Start",
    "stop": "Stopp",
    "clear": "Löschen",
    "seen": "erkannt",
    "selected": "ausgewählt",
    "all": "alle",
    "errors_only": "Nur Fehler",
    "packets": "Pakete",
    "error": "Fehler",
    "errors": "Fehler",
    "col_time": "Zeit",
    "col_sender": "Absender",
    "col_slave": "Slave",
    "col_function": "Funktion",
    "col_fc": "Funktionscode",
    "col_payload": "Payload",
    "col_bytes": "Bytes",
    "col_crc": "CRC",
    "facet_slave_id": "Slave-ID",
    "facet_function_code": "Funktionscode",
    "parsed": "Analysiert",
    "start_addr": "Startadresse",
    "quantity": "Anzahl",
    "size": "Größe",
    "raw_bytes": "Rohbytes",
    "export_csv": "CSV exportieren",
    "hide_errors": "Fehler ausblenden",
    "send_packet": "Paket senden",
    "resend": "Erneut senden",
    "resend_sent": "{n} Bytes an Port {port} gesendet",
    "resend_tx_disabled": "TX für diesen Port deaktiviert",
    "resend_unavailable": "Dieses Paket kann nicht erneut gesendet werden",
    "resend_crc_err": "Ein Frame mit CRC-Fehler kann nicht erneut gesendet werden",
    "capture_autostarted": "Erfassung automatisch gestartet",
    "capture_not_running": "Ohne laufende Erfassung gesendet, die Pakete erscheinen nicht in der Liste",
    "port_n": "Port {n}"
  }
}
</i18n>
