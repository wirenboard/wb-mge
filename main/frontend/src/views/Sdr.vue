<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';
import { api, apiBinary } from '@/utils/api';

interface SdrInfo {
  gain_max: number;
  start_mhz: number;
  stop_mhz: number;
  bin_khz: number;
  dwell_max_ms: number;
}

// Query of GET /sdr/scan. A type alias rather than an interface, so it passes as ky's searchParams.
type ScanParams = {
  gain: number;
  dwell_ms: number;
};

// A whole device scan: bin i is centred at startMhz + i * binMhz.
interface Frame {
  dbfs: Int8Array;
  startMhz: number;
  binMhz: number;
}

// The display row being collected: the per-bin maximum of its scans so far.
interface Row {
  peak: Int8Array;
  startedAt: number;
}

interface WifiChannel {
  number: number;
  mhz: number;
}

type SecondaryChannel = 'none' | 'above' | 'below';

interface WifiNetwork {
  ssid: string;
  bssid: string;
  channel: number;
  second: SecondaryChannel;
  rssi: number;
}

const DEFAULT_START_MHZ = 2400;
const DEFAULT_STOP_MHZ = 2500;
const DEFAULT_GAIN = 40;
const GAIN_AGC = -1;
const GAIN_HEADER = 'X-Sdr-Gain';
const DWELL_OPTIONS_MS = [0, 50, 100, 200, 500, 1000, 2000];
const SWEEP_OPTIONS_MS = [0, 250, 500, 1000, 2000, 5000];
const DEFAULT_DWELL_MS = 0;
const DEFAULT_SWEEP_MS = 0;
// The device listens for the dwell time at each of the two frequencies a scan tunes to.
const SCAN_TUNINGS = 2;
const SCAN_BASE_TIMEOUT_MS = 5500;
const NETWORKS_TIMEOUT_MS = 10000;
const KHZ_PER_MHZ = 1000;
const MS_PER_S = 1000;
const CURSOR_DECIMALS = 2;
const FLOOR_MIN_DB = -140;
const FLOOR_MAX_DB = -40;
const DEFAULT_FLOOR_DB = -100;
const RANGE_MIN_DB = 20;
const RANGE_MAX_DB = 120;
const DEFAULT_RANGE_DB = 80;

const LUT_SIZE = 256;
const WATERFALL_STOPS: ReadonlyArray<readonly [number, readonly [number, number, number]]> = [
  [0, [8, 10, 24]],
  [0.35, [28, 72, 210]],
  [0.7, [246, 212, 44]],
  [1, [255, 255, 255]],
];
const GRID_COLOR = 'rgba(255, 255, 255, 0.1)';
const LABEL_COLOR = '#9aa3b0';
const TRACE_COLOR = '#5ccf7a';
const LABEL_FONT_PX = 11;
const LABEL_PAD_PX = 4;
const CHANNEL_TICK_PX = 4;
const CHANNEL_ROW_PX = 18;
const FREQ_ROW_PX = 18;
const AXIS_HEIGHT_PX = CHANNEL_ROW_PX + FREQ_ROW_PX;
const CURSOR_COLOR = 'rgba(255, 255, 255, 0.7)';
const CURSOR_TEXT_COLOR = '#ffffff';
const CURSOR_BOX_PAD_PX = 2;

const WIFI_CHANNEL_1_MHZ = 2412;
const WIFI_CHANNEL_STEP_MHZ = 5;
const WIFI_CHANNEL_14 = 14;
const WIFI_CHANNEL_14_MHZ = 2484;

const wifiChannelMhz = (channel: number): number => ((channel === WIFI_CHANNEL_14)
  ? WIFI_CHANNEL_14_MHZ
  : WIFI_CHANNEL_1_MHZ + (WIFI_CHANNEL_STEP_MHZ * (channel - 1)));

const WIFI_CHANNELS: ReadonlyArray<WifiChannel> = Array.from({ length: WIFI_CHANNEL_14 }, (_, index) => ({
  number: index + 1,
  mhz: wifiChannelMhz(index + 1),
}));

const NETWORK_WIDTH_MHZ = 20;
const NETWORK_WIDE_WIDTH_MHZ = 40;
// The top edge of a network's trapezoid is this much narrower than its base on each side.
const NETWORK_SHOULDER_MHZ = 2;
// A 40 MHz network is centred between its primary channel and the secondary one.
const NETWORK_CENTRE_SHIFT_MHZ: Record<SecondaryChannel, number> = { none: 0, above: 10, below: -10 };
const NETWORK_RSSI_MIN_DBM = -100;
const NETWORK_RSSI_MAX_DBM = -20;
const NETWORK_FILL_ALPHA = 0.18;
const NETWORK_PALETTE = ['#ff6b6b', '#4dabf7', '#ffa94d', '#b197fc', '#3bc9db', '#ffd43b', '#f783ac'];
const BSSID_HASH_MULTIPLIER = 31;

// The same BSSID keeps its colour from one scan of networks to the next.
const networkColor = (bssid: string): string => {
  let hash = 0;
  for (let index = 0; index < bssid.length; index++) {
    hash = ((hash * BSSID_HASH_MULTIPLIER) + bssid.charCodeAt(index)) >>> 0;
  }
  return NETWORK_PALETTE[hash % NETWORK_PALETTE.length];
};

const buildWaterfallLut = (): Uint8ClampedArray => {
  const lut = new Uint8ClampedArray(LUT_SIZE * 3);
  let stop = 0;
  for (let index = 0; index < LUT_SIZE; index++) {
    const position = index / (LUT_SIZE - 1);
    while ((stop < WATERFALL_STOPS.length - 2) && (position > WATERFALL_STOPS[stop + 1][0])) {
      stop++;
    }
    const [from, fromColor] = WATERFALL_STOPS[stop];
    const [to, toColor] = WATERFALL_STOPS[stop + 1];
    const mix = (position - from) / (to - from);
    for (let channel = 0; channel < 3; channel++) {
      lut[(index * 3) + channel] = fromColor[channel] + ((toColor[channel] - fromColor[channel]) * mix);
    }
  }
  return lut;
};

const WATERFALL_LUT = buildWaterfallLut();
// The spectrum shares the waterfall's bottom colour, so the two plots read as one.
const BACKGROUND_COLOR = `rgb(${WATERFALL_LUT[0]}, ${WATERFALL_LUT[1]}, ${WATERFALL_LUT[2]})`;

// Keeps four to eight horizontal grid lines across the whole dynamic range slider.
const gridStepDb = (range: number): number => {
  if (range <= 40) {
    return 5;
  }
  return range <= 80 ? 10 : 20;
};

const { t, locale } = useI18n();

const info = ref<SdrInfo>();
// The fields hold what was typed; the plots show the last pair that passed validation.
const startMhz = ref(DEFAULT_START_MHZ);
const stopMhz = ref(DEFAULT_STOP_MHZ);
const scanStartMhz = ref(DEFAULT_START_MHZ);
const scanStopMhz = ref(DEFAULT_STOP_MHZ);
const agc = ref(true);
// Under AGC this follows the gain the device reports, so switching AGC off keeps that gain as the manual one.
const gain = ref(DEFAULT_GAIN);
const dwellMs = ref(DEFAULT_DWELL_MS);
const sweepMs = ref(DEFAULT_SWEEP_MS);
const floorDb = ref(DEFAULT_FLOOR_DB);
const rangeDb = ref(DEFAULT_RANGE_DB);
const isRunning = ref(false);
const errorKey = ref('');
const isScanningNetworks = ref(false);
const networksErrorKey = ref('');

const spectrumCanvas = ref<HTMLCanvasElement>();
const waterfallCanvas = ref<HTMLCanvasElement>();
const overlayCanvas = ref<HTMLCanvasElement>();

let lastFrame: Frame | null = null;
let pendingRow: Row | null = null;
let networks: WifiNetwork[] = [];
// Pointer position over the plots in CSS pixels, null while the pointer is elsewhere.
let cursorX: number | null = null;
let loop: Promise<void> | null = null;
let resizeObserver: ResizeObserver | null = null;

const dwellOptions = computed((): number[] =>
  DWELL_OPTIONS_MS.filter((ms) => !info.value || (ms <= info.value.dwell_max_ms)));

const dwellLabel = (ms: number): string => (ms ? t('value_ms', { value: ms }) : t('dwell_single'));

const sweepLabel = (ms: number): string =>
  (ms ? t('value_s', { value: (ms / MS_PER_S).toLocaleString(locale.value) }) : t('sweep_every'));

// v-model.number leaves an empty or unparsable field as a string, which the integer check rejects.
const spanError = computed((): string => {
  const start = startMhz.value;
  const stop = stopMhz.value;
  if (!Number.isInteger(start) || !Number.isInteger(stop)) {
    return t('error_span_integer');
  }
  const limits = info.value;
  if (!limits) {
    return '';
  }
  if ((start < limits.start_mhz) || (stop > limits.stop_mhz)) {
    return t('error_span_limits', { min: limits.start_mhz, max: limits.stop_mhz });
  }
  if (start >= stop) {
    return t('error_span_order');
  }
  return '';
});

const scanParams = (): ScanParams => ({
  gain: agc.value ? GAIN_AGC : gain.value,
  dwell_ms: dwellMs.value,
});

const isSameScan = (a: ScanParams, b: ScanParams): boolean => (a.gain === b.gain) && (a.dwell_ms === b.dwell_ms);

const scanTimeoutMs = (params: ScanParams): number => (SCAN_TUNINGS * params.dwell_ms) + SCAN_BASE_TIMEOUT_MS;

const expectedBins = (limits: SdrInfo): number =>
  Math.round(((limits.stop_mhz - limits.start_mhz) * KHZ_PER_MHZ) / limits.bin_khz);

// One value per pixel column of the window: the strongest bin overlapping the column, so the frame's bins
// outside the window are cropped away. A column no bin reaches stays NaN.
const columnPeaks = (frame: Frame, windowStartMhz: number, windowStopMhz: number, columns: number): Float32Array => {
  const { dbfs, binMhz } = frame;
  const peaks = new Float32Array(columns).fill(NaN);
  const mhzPerColumn = (windowStopMhz - windowStartMhz) / columns;
  // Bins are centred on their frequency, so bin i spans i - 0.5 .. i + 0.5 in bin units
  const windowFirstBin = ((windowStartMhz - frame.startMhz) / binMhz) + 0.5;
  for (let x = 0; x < columns; x++) {
    const first = Math.floor(windowFirstBin + ((x * mhzPerColumn) / binMhz));
    const last = Math.min(dbfs.length,
      Math.max(first + 1, Math.ceil(windowFirstBin + (((x + 1) * mhzPerColumn) / binMhz))));
    for (let bin = first; bin < last; bin++) {
      peaks[x] = (bin === first) ? dbfs[bin] : Math.max(peaks[x], dbfs[bin]);
    }
  }
  return peaks;
};

// api() and apiBinary() already turn a timeout and a lost session into plain messages; a 400 is the
// device refusing the scan parameters and still carries ky's response.
const errorKeyFor = (err: unknown, fallback: string): string => {
  const message = (err as Error | undefined)?.message;
  if (message === 'timeout') {
    return 'error_timeout';
  }
  if (message === 'unauthorized') {
    return 'error_unauthorized';
  }
  if ((err as { response?: Response } | undefined)?.response?.status === 400) {
    return 'error_rejected';
  }
  return fallback;
};

// Returns whether the backing store was resized: the browser blanks a canvas whose size changes.
const fitCanvas = (canvas: HTMLCanvasElement): boolean => {
  const ratio = window.devicePixelRatio || 1;
  const width = Math.max(1, Math.round(canvas.clientWidth * ratio));
  const height = Math.max(1, Math.round(canvas.clientHeight * ratio));
  if ((canvas.width === width) && (canvas.height === height)) {
    return false;
  }
  canvas.width = width;
  canvas.height = height;
  return true;
};

// The first axis row carries the Wi-Fi channel ticks, the second the start, middle and stop frequencies.
const drawFrequencyAxis = (ctx: CanvasRenderingContext2D, width: number, plotHeight: number) => {
  const start = scanStartMhz.value;
  const stop = scanStopMhz.value;
  const xOf = (mhz: number): number => ((mhz - start) * width) / (stop - start);

  ctx.strokeStyle = LABEL_COLOR;
  ctx.fillStyle = LABEL_COLOR;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'top';
  let labelFreeFromX = -Infinity;
  for (const channel of WIFI_CHANNELS) {
    if ((channel.mhz < start) || (channel.mhz > stop)) {
      continue;
    }
    const x = Math.min(width - 1, Math.round(xOf(channel.mhz))) + 0.5;
    ctx.beginPath();
    ctx.moveTo(x, plotHeight);
    ctx.lineTo(x, plotHeight + CHANNEL_TICK_PX);
    ctx.stroke();

    const label = String(channel.number);
    const halfWidth = ctx.measureText(label).width / 2;
    const labelX = Math.min(width - halfWidth, Math.max(halfWidth, x));
    // Over a wide span the channels crowd together; a number that would overlap the previous one is skipped.
    if ((labelX - halfWidth) >= labelFreeFromX) {
      ctx.fillText(label, labelX, plotHeight + CHANNEL_TICK_PX);
      labelFreeFromX = labelX + halfWidth + LABEL_PAD_PX;
    }
  }

  const labels: Array<[number, CanvasTextAlign, number]> = [
    [LABEL_PAD_PX, 'left', start],
    [width / 2, 'center', (start + stop) / 2],
    [width - LABEL_PAD_PX, 'right', stop],
  ];
  ctx.textBaseline = 'middle';
  for (const [x, align, mhz] of labels) {
    ctx.textAlign = align;
    ctx.fillText(`${mhz} ${t('unit_mhz')}`, x, plotHeight + CHANNEL_ROW_PX + (FREQ_ROW_PX / 2));
  }
};

// Each network is a trapezoid over its channel, as tall as its RSSI on a fixed dBm scale that ignores the
// floor and range sliders, with its SSID and RSSI above it.
const drawNetworks = (ctx: CanvasRenderingContext2D, width: number, plotHeight: number) => {
  const start = scanStartMhz.value;
  const stop = scanStopMhz.value;
  const xOf = (mhz: number): number => ((mhz - start) * width) / (stop - start);

  ctx.save();
  ctx.beginPath();
  ctx.rect(0, 0, width, plotHeight);
  ctx.clip();
  ctx.textAlign = 'center';
  for (const network of networks) {
    const centreMhz = wifiChannelMhz(network.channel) + NETWORK_CENTRE_SHIFT_MHZ[network.second];
    const halfBaseMhz = ((network.second === 'none') ? NETWORK_WIDTH_MHZ : NETWORK_WIDE_WIDTH_MHZ) / 2;
    const halfTopMhz = halfBaseMhz - NETWORK_SHOULDER_MHZ;
    if (((centreMhz + halfBaseMhz) <= start) || ((centreMhz - halfBaseMhz) >= stop)) {
      continue;
    }
    const level = (network.rssi - NETWORK_RSSI_MIN_DBM) / (NETWORK_RSSI_MAX_DBM - NETWORK_RSSI_MIN_DBM);
    const top = plotHeight * (1 - Math.min(1, Math.max(0, level)));
    const color = networkColor(network.bssid);

    ctx.beginPath();
    ctx.moveTo(xOf(centreMhz - halfBaseMhz), plotHeight);
    ctx.lineTo(xOf(centreMhz - halfTopMhz), top);
    ctx.lineTo(xOf(centreMhz + halfTopMhz), top);
    ctx.lineTo(xOf(centreMhz + halfBaseMhz), plotHeight);
    ctx.closePath();
    ctx.globalAlpha = NETWORK_FILL_ALPHA;
    ctx.fillStyle = color;
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.strokeStyle = color;
    ctx.stroke();

    const label = `${network.ssid || t('hidden_network')} ${network.rssi} ${t('unit_dbm')}`;
    const halfLabelWidth = ctx.measureText(label).width / 2;
    const labelX = Math.min(width - halfLabelWidth, Math.max(halfLabelWidth, xOf(centreMhz)));
    // A label above a trapezoid that reaches the top would be cut off by the canvas edge.
    const isBelow = top < (LABEL_FONT_PX + LABEL_PAD_PX);
    ctx.textBaseline = isBelow ? 'top' : 'bottom';
    ctx.fillText(label, labelX, isBelow ? (top + 2) : (top - 2));
  }
  ctx.restore();
};

const drawSpectrum = () => {
  const canvas = spectrumCanvas.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx || !canvas.clientWidth || !canvas.clientHeight) {
    return;
  }
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  ctx.setTransform(canvas.width / width, 0, 0, canvas.height / height, 0, 0);
  ctx.fillStyle = BACKGROUND_COLOR;
  ctx.fillRect(0, 0, width, height);

  const floor = floorDb.value;
  const range = rangeDb.value;
  const plotHeight = height - AXIS_HEIGHT_PX;
  const yOf = (db: number) => plotHeight * (1 - ((db - floor) / range));

  ctx.font = `${LABEL_FONT_PX}px ${getComputedStyle(canvas).fontFamily}`;
  ctx.lineWidth = 1;
  ctx.strokeStyle = GRID_COLOR;
  ctx.fillStyle = LABEL_COLOR;
  ctx.textAlign = 'left';
  const step = gridStepDb(range);
  for (let db = Math.ceil(floor / step) * step; db <= floor + range; db += step) {
    const y = Math.round(yOf(db)) + 0.5;
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(width, y);
    ctx.stroke();
    // A label above the topmost line would be cut off by the canvas edge.
    const isBelow = y < (LABEL_FONT_PX + LABEL_PAD_PX);
    ctx.textBaseline = isBelow ? 'top' : 'bottom';
    ctx.fillText(String(db), LABEL_PAD_PX, isBelow ? (y + 2) : (y - 2));
  }

  drawNetworks(ctx, width, plotHeight);

  if (lastFrame) {
    const peaks = columnPeaks(lastFrame, scanStartMhz.value, scanStopMhz.value, width);
    ctx.strokeStyle = TRACE_COLOR;
    ctx.beginPath();
    for (let x = 0; (x < width) && !Number.isNaN(peaks[x]); x++) {
      const y = Math.min(plotHeight, Math.max(0, yOf(peaks[x])));
      if (x === 0) {
        ctx.moveTo(x + 0.5, y);
      } else {
        ctx.lineTo(x + 0.5, y);
      }
    }
    ctx.stroke();
  }

  drawFrequencyAxis(ctx, width, plotHeight);
};

const clearWaterfall = () => {
  const canvas = waterfallCanvas.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx) {
    return;
  }
  ctx.fillStyle = BACKGROUND_COLOR;
  ctx.fillRect(0, 0, canvas.width, canvas.height);
};

const paintWaterfallRow = (frame: Frame) => {
  const canvas = waterfallCanvas.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx) {
    return;
  }
  const { width, height } = canvas;
  if (height > 1) {
    ctx.drawImage(canvas, 0, 0, width, height - 1, 0, 1, width, height - 1);
  }

  const row = ctx.createImageData(width, 1);
  const floor = floorDb.value;
  const range = rangeDb.value;
  const peaks = columnPeaks(frame, scanStartMhz.value, scanStopMhz.value, width);
  for (let x = 0; x < width; x++) {
    // A column no bin reaches gets the background colour.
    const level = Number.isNaN(peaks[x]) ? 0 : Math.min(1, Math.max(0, (peaks[x] - floor) / range));
    const color = Math.round(level * (LUT_SIZE - 1)) * 3;
    const pixel = x * 4;
    row.data[pixel] = WATERFALL_LUT[color];
    row.data[pixel + 1] = WATERFALL_LUT[color + 1];
    row.data[pixel + 2] = WATERFALL_LUT[color + 2];
    row.data[pixel + 3] = 255;
  }
  ctx.putImageData(row, 0, 0);
};

// The cursor lives on its own layer over both plots, so it moves without a new scan and never lands in the
// scrolling waterfall image.
const drawCursor = () => {
  const canvas = overlayCanvas.value;
  const ctx = canvas?.getContext('2d');
  if (!canvas || !ctx || !canvas.clientWidth || !canvas.clientHeight) {
    return;
  }
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  ctx.setTransform(canvas.width / width, 0, 0, canvas.height / height, 0, 0);
  ctx.clearRect(0, 0, width, height);
  if (cursorX === null) {
    return;
  }

  const x = Math.min(width - 1, Math.round(cursorX)) + 0.5;
  ctx.lineWidth = 1;
  ctx.strokeStyle = CURSOR_COLOR;
  ctx.beginPath();
  ctx.moveTo(x, 0);
  ctx.lineTo(x, height);
  ctx.stroke();

  const start = scanStartMhz.value;
  const mhz = start + ((x * (scanStopMhz.value - start)) / width);
  const label = `${mhz.toLocaleString(locale.value, {
    minimumFractionDigits: CURSOR_DECIMALS,
    maximumFractionDigits: CURSOR_DECIMALS,
    useGrouping: false,
  })} ${t('unit_mhz')}`;
  ctx.font = `${LABEL_FONT_PX}px ${getComputedStyle(canvas).fontFamily}`;
  ctx.textAlign = 'left';
  ctx.textBaseline = 'top';
  const labelWidth = ctx.measureText(label).width;
  // The readout sits right of the line, or left of it where it would run off the canvas.
  const labelX = ((x + LABEL_PAD_PX + labelWidth) > width) ? (x - LABEL_PAD_PX - labelWidth) : (x + LABEL_PAD_PX);
  ctx.fillStyle = BACKGROUND_COLOR;
  ctx.fillRect(labelX - CURSOR_BOX_PAD_PX, LABEL_PAD_PX - CURSOR_BOX_PAD_PX,
    labelWidth + (2 * CURSOR_BOX_PAD_PX), LABEL_FONT_PX + (2 * CURSOR_BOX_PAD_PX));
  ctx.fillStyle = CURSOR_TEXT_COLOR;
  ctx.fillText(label, labelX, LABEL_PAD_PX);
};

// Folds a scan into the current row; returns the row's per-bin maximum once the sweep time has run out,
// counted from the request of the row's first scan.
const collectRow = (dbfs: Int8Array, requestedAt: number): Int8Array | null => {
  if (!pendingRow) {
    pendingRow = { peak: dbfs, startedAt: requestedAt };
  } else {
    for (let bin = 0; bin < dbfs.length; bin++) {
      pendingRow.peak[bin] = Math.max(pendingRow.peak[bin], dbfs[bin]);
    }
  }
  if ((performance.now() - pendingRow.startedAt) < sweepMs.value) {
    return null;
  }
  const { peak } = pendingRow;
  pendingRow = null;
  return peak;
};

const scanLoop = async () => {
  while (isRunning.value && info.value) {
    const limits = info.value;
    const params = scanParams();
    const requestedAt = performance.now();
    try {
      const { body, headers } = await apiBinary('sdr/scan', {
        searchParams: params,
        timeout: scanTimeoutMs(params),
      });
      // A sweep requested before the last gain or dwell change no longer matches the controls.
      if (!isRunning.value || !isSameScan(params, scanParams())) {
        continue;
      }
      const dbfs = new Int8Array(body);
      const expected = expectedBins(limits);
      // The device ends the body early when a capture fails mid-sweep.
      if (dbfs.length !== expected) {
        throw new Error(`SDR scan returned ${dbfs.length} bins instead of ${expected}`);
      }
      const usedGain = Number.parseInt(headers.get(GAIN_HEADER) ?? '', 10);
      if ((params.gain === GAIN_AGC) && Number.isInteger(usedGain)) {
        gain.value = usedGain;
      }
      const peak = collectRow(dbfs, requestedAt);
      if (!peak) {
        continue;
      }
      lastFrame = { dbfs: peak, startMhz: limits.start_mhz, binMhz: limits.bin_khz / KHZ_PER_MHZ };
      drawSpectrum();
      paintWaterfallRow(lastFrame);
    } catch (err) {
      if (isRunning.value) {
        console.error('SDR scan failed', err);
        errorKey.value = errorKeyFor(err, 'error_capture');
        isRunning.value = false;
      }
    }
  }
};

const start = () => {
  errorKey.value = '';
  // A row left unfinished by Stop would otherwise close at once with maxima from before the pause.
  pendingRow = null;
  isRunning.value = true;
  // After Stop the previous loop may still be waiting for its last sweep; it then simply carries
  // on, so a quick Stop/Start never puts two requests in flight.
  if (!loop) {
    loop = scanLoop().finally(() => {
      loop = null;
    });
  }
};

const stop = () => {
  isRunning.value = false;
};

// A failed scan of networks clears the previous result as well: it no longer describes the air.
const scanNetworks = async () => {
  networksErrorKey.value = '';
  isScanningNetworks.value = true;
  try {
    networks = await api<WifiNetwork[]>('sdr/networks', { timeout: NETWORKS_TIMEOUT_MS });
  } catch (err) {
    console.error('Wi-Fi network scan failed', err);
    networks = [];
    networksErrorKey.value = errorKeyFor(err, 'error_networks');
  } finally {
    isScanningNetworks.value = false;
  }
  drawSpectrum();
};

const onPlotsPointerMove = (event: PointerEvent) => {
  const bounds = (event.currentTarget as HTMLElement).getBoundingClientRect();
  cursorX = Math.min(bounds.width, Math.max(0, event.clientX - bounds.left));
  drawCursor();
};

const onPlotsPointerLeave = () => {
  cursorX = null;
  drawCursor();
};

const onResize = () => {
  if (spectrumCanvas.value && fitCanvas(spectrumCanvas.value)) {
    drawSpectrum();
  }
  if (waterfallCanvas.value && fitCanvas(waterfallCanvas.value)) {
    clearWaterfall();
  }
  if (overlayCanvas.value && fitCanvas(overlayCanvas.value)) {
    drawCursor();
  }
};

watch([startMhz, stopMhz, info], () => {
  if (info.value && !spanError.value) {
    scanStartMhz.value = startMhz.value;
    scanStopMhz.value = stopMhz.value;
  }
});

// Every frame covers the whole device range, so a new window only redraws the last one; the waterfall rows
// of the old window are dropped.
watch([scanStartMhz, scanStopMhz], () => {
  clearWaterfall();
  drawSpectrum();
  drawCursor();
});
watch([floorDb, rangeDb], () => drawSpectrum());

// A gain or dwell change discards the partial row. The gain reported under AGC lands in `gain` as well, but the
// requested gain stays at AGC then, so those updates keep the row.
watch([() => scanParams().gain, () => scanParams().dwell_ms], () => {
  pendingRow = null;
});

onMounted(async () => {
  resizeObserver = new ResizeObserver(onResize);
  for (const canvas of [spectrumCanvas.value, waterfallCanvas.value, overlayCanvas.value]) {
    if (canvas) {
      resizeObserver.observe(canvas);
    }
  }
  onResize();

  try {
    const sdrInfo = await api<SdrInfo>('sdr/info');
    gain.value = Math.min(DEFAULT_GAIN, sdrInfo.gain_max);
    info.value = sdrInfo;
  } catch (err) {
    console.error('Failed to read the SDR parameters', err);
    errorKey.value = errorKeyFor(err, 'error_info');
  }
});

onBeforeUnmount(() => {
  isRunning.value = false;
  resizeObserver?.disconnect();
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <div class="grid-2">
        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('receiver') }}</div>
            <div class="sdr-actions">
              <Button type="button" variant="outline" :disabled="isScanningNetworks" @click="scanNetworks">
                {{ isScanningNetworks ? t('scanning_networks') : t('scan_networks') }}
              </Button>
              <Button
                type="button"
                :variant="isRunning ? 'outline' : 'primary'"
                :disabled="!info || (!isRunning && !!spanError)"
                @click="isRunning ? stop() : start()"
              >
                {{ isRunning ? t('stop') : t('start') }}
              </Button>
            </div>
          </div>
          <div class="card-body sdr-controls">
            <div class="field">
              <label for="sdr_start">{{ t('start_freq') }}</label>
              <input
                id="sdr_start"
                v-model.lazy.number="startMhz"
                type="number"
                step="1"
                :min="info?.start_mhz"
                :max="info?.stop_mhz"
              >
            </div>
            <div class="field">
              <label for="sdr_stop">{{ t('stop_freq') }}</label>
              <input
                id="sdr_stop"
                v-model.lazy.number="stopMhz"
                type="number"
                step="1"
                :min="info?.start_mhz"
                :max="info?.stop_mhz"
              >
            </div>
            <div class="field">
              <label for="sdr_gain">{{ t('gain') }}</label>
              <div class="sdr-inline">
                <Switch id="sdr_agc" v-model="agc" :aria-label="t('agc_aria')" />
                <span class="sdr-caption">{{ t('agc') }}</span>
                <input
                  id="sdr_gain"
                  v-model.number="gain"
                  class="sdr-slider"
                  type="range"
                  min="0"
                  :max="info?.gain_max"
                  :disabled="agc"
                >
                <span class="mono sdr-value">{{ gain }}</span>
              </div>
            </div>
            <div class="field">
              <label for="sdr_dwell">{{ t('dwell') }}</label>
              <select id="sdr_dwell" v-model="dwellMs">
                <option v-for="ms in dwellOptions" :key="ms" :value="ms">{{ dwellLabel(ms) }}</option>
              </select>
            </div>
            <div v-if="spanError" class="sdr-error" role="alert">{{ spanError }}</div>
            <div v-if="errorKey" class="sdr-error" role="alert">{{ t(errorKey) }}</div>
            <div v-if="networksErrorKey" class="sdr-error" role="alert">{{ t(networksErrorKey) }}</div>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('display') }}</div>
          </div>
          <div class="card-body sdr-controls">
            <div class="field">
              <label for="sdr_floor">{{ t('floor') }}</label>
              <div class="sdr-inline">
                <input
                  id="sdr_floor"
                  v-model.number="floorDb"
                  class="sdr-slider"
                  type="range"
                  :min="FLOOR_MIN_DB"
                  :max="FLOOR_MAX_DB"
                >
                <span class="mono sdr-value">{{ floorDb }}</span>
              </div>
            </div>
            <div class="field">
              <label for="sdr_range">{{ t('range') }}</label>
              <div class="sdr-inline">
                <input
                  id="sdr_range"
                  v-model.number="rangeDb"
                  class="sdr-slider"
                  type="range"
                  :min="RANGE_MIN_DB"
                  :max="RANGE_MAX_DB"
                >
                <span class="mono sdr-value">{{ rangeDb }}</span>
              </div>
            </div>
            <div class="field">
              <label for="sdr_sweep">{{ t('sweep') }}</label>
              <select id="sdr_sweep" v-model="sweepMs">
                <option v-for="ms in SWEEP_OPTIONS_MS" :key="ms" :value="ms">{{ sweepLabel(ms) }}</option>
              </select>
            </div>
          </div>
        </section>
      </div>

      <section class="card">
        <div class="card-header">
          <div class="title">{{ t('plots') }}</div>
        </div>
        <div class="card-body sdr-plots">
          <div class="sdr-stack" @pointermove="onPlotsPointerMove" @pointerleave="onPlotsPointerLeave">
            <canvas ref="spectrumCanvas" class="sdr-spectrum" role="img" :aria-label="t('spectrum')" />
            <canvas ref="waterfallCanvas" class="sdr-waterfall" role="img" :aria-label="t('waterfall')" />
            <canvas ref="overlayCanvas" class="sdr-overlay" aria-hidden="true" />
          </div>
        </div>
      </section>
    </div>
  </Layout>
</template>

<style scoped>
.sdr-actions {
  display: flex;
  gap: 8px;
}

.sdr-controls .field {
  grid-template-columns: minmax(120px, 1fr) minmax(0, 240px);
}

.sdr-inline {
  display: flex;
  align-items: center;
  gap: 8px;
  min-width: 0;
}

.sdr-caption {
  color: var(--text-secondary);
}

.sdr-slider {
  flex: 1;
  min-width: 0;
  accent-color: var(--primary-color);
}

.sdr-value {
  min-width: 4ch;
  text-align: right;
}

.sdr-error {
  padding-top: 10px;
  color: var(--danger-color);
}

.sdr-plots {
  padding-top: 14px;
}

.sdr-stack {
  position: relative;
}

.sdr-spectrum,
.sdr-waterfall {
  display: block;
  width: 100%;
}

.sdr-overlay {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  pointer-events: none;
}

.sdr-spectrum {
  height: 220px;
  border-radius: var(--r-sm) var(--r-sm) 0 0;
}

.sdr-waterfall {
  height: 320px;
  border-radius: 0 0 var(--r-sm) var(--r-sm);
}
</style>

<i18n>
{
  "en": {
    "title": "SDR band scanner",
    "crumbs": "Swept spectrum and waterfall with Wi-Fi channel markers",
    "receiver": "Receiver",
    "display": "Display",
    "plots": "Spectrum and waterfall",
    "spectrum": "Spectrum",
    "waterfall": "Waterfall",
    "start_freq": "Start frequency, MHz",
    "stop_freq": "Stop frequency, MHz",
    "gain": "Gain",
    "agc": "AGC",
    "agc_aria": "Automatic gain control",
    "floor": "Display floor, dBFS",
    "range": "Dynamic range, dB",
    "start": "Start",
    "stop": "Stop",
    "dwell": "Dwell per frequency",
    "dwell_single": "Single capture",
    "sweep": "Sweep time",
    "sweep_every": "Every scan",
    "scan_networks": "Scan networks",
    "scanning_networks": "Scanning…",
    "hidden_network": "Hidden network",
    "unit_mhz": "MHz",
    "unit_dbm": "dBm",
    "value_ms": "{value} ms",
    "value_s": "{value} s",
    "error_span_integer": "Enter the frequencies in whole MHz",
    "error_span_limits": "The frequencies must lie within {min}–{max} MHz",
    "error_span_order": "The start frequency must be below the stop frequency",
    "error_info": "Could not read the receiver parameters from the device",
    "error_capture": "Could not get the spectrum from the device",
    "error_networks": "Could not scan the Wi-Fi networks",
    "error_rejected": "The device rejected the scan parameters",
    "error_timeout": "The device did not answer in time",
    "error_unauthorized": "The session has expired, log in again"
  },
  "ru": {
    "title": "SDR-сканер диапазона",
    "crumbs": "Спектр по развёртке и водопад с метками каналов Wi-Fi",
    "receiver": "Приёмник",
    "display": "Отображение",
    "plots": "Спектр и водопад",
    "spectrum": "Спектр",
    "waterfall": "Водопад",
    "start_freq": "Начальная частота, МГц",
    "stop_freq": "Конечная частота, МГц",
    "gain": "Усиление",
    "agc": "АРУ",
    "agc_aria": "Автоматическая регулировка усиления",
    "floor": "Нижний уровень, дБFS",
    "range": "Динамический диапазон, дБ",
    "start": "Запустить",
    "stop": "Остановить",
    "dwell": "Время на частоте",
    "dwell_single": "Один захват",
    "sweep": "Время развёртки",
    "sweep_every": "Каждое сканирование",
    "scan_networks": "Сканировать сети",
    "scanning_networks": "Сканирование…",
    "hidden_network": "Скрытая сеть",
    "unit_mhz": "МГц",
    "unit_dbm": "дБм",
    "value_ms": "{value} мс",
    "value_s": "{value} с",
    "error_span_integer": "Укажите частоты целым числом МГц",
    "error_span_limits": "Частоты должны быть в пределах {min}–{max} МГц",
    "error_span_order": "Начальная частота должна быть ниже конечной",
    "error_info": "Не удалось получить параметры приёмника",
    "error_capture": "Не удалось получить спектр с устройства",
    "error_networks": "Не удалось просканировать сети Wi-Fi",
    "error_rejected": "Устройство отклонило параметры сканирования",
    "error_timeout": "Устройство не ответило вовремя",
    "error_unauthorized": "Сессия истекла, войдите снова"
  },
  "kk": {
    "title": "SDR диапазон сканері",
    "crumbs": "Wi-Fi арна белгілері бар спектр және сарқырама",
    "receiver": "Қабылдағыш",
    "display": "Көрсету",
    "plots": "Спектр және сарқырама",
    "spectrum": "Спектр",
    "waterfall": "Сарқырама",
    "start_freq": "Бастапқы жиілік, МГц",
    "stop_freq": "Соңғы жиілік, МГц",
    "gain": "Күшейту",
    "agc": "AGC",
    "agc_aria": "Күшейтуді автоматты реттеу",
    "floor": "Төменгі деңгей, dBFS",
    "range": "Динамикалық диапазон, dB",
    "start": "Іске қосу",
    "stop": "Тоқтату",
    "dwell": "Жиілікті тыңдау уақыты",
    "dwell_single": "Бір рет түсіру",
    "sweep": "Бір жолдың уақыты",
    "sweep_every": "Әр сканерлеу",
    "scan_networks": "Желілерді сканерлеу",
    "scanning_networks": "Сканерлеу…",
    "hidden_network": "Жасырын желі",
    "unit_mhz": "МГц",
    "unit_dbm": "dBm",
    "value_ms": "{value} мс",
    "value_s": "{value} с",
    "error_span_integer": "Жиіліктерді бүтін МГц санымен енгізіңіз",
    "error_span_limits": "Жиіліктер {min}–{max} МГц аралығында болуы керек",
    "error_span_order": "Бастапқы жиілік соңғы жиіліктен төмен болуы керек",
    "error_info": "Қабылдағыш параметрлерін алу мүмкін болмады",
    "error_capture": "Құрылғыдан спектрді алу мүмкін болмады",
    "error_networks": "Wi-Fi желілерін сканерлеу мүмкін болмады",
    "error_rejected": "Құрылғы сканерлеу параметрлерін қабылдамады",
    "error_timeout": "Құрылғы уақытында жауап бермеді",
    "error_unauthorized": "Сеанс мерзімі аяқталды, қайта кіріңіз"
  },
  "it": {
    "title": "Scanner di banda SDR",
    "crumbs": "Spettro a scansione e waterfall con i canali Wi-Fi",
    "receiver": "Ricevitore",
    "display": "Visualizzazione",
    "plots": "Spettro e waterfall",
    "spectrum": "Spettro",
    "waterfall": "Waterfall",
    "start_freq": "Frequenza iniziale, MHz",
    "stop_freq": "Frequenza finale, MHz",
    "gain": "Guadagno",
    "agc": "AGC",
    "agc_aria": "Controllo automatico del guadagno",
    "floor": "Livello minimo, dBFS",
    "range": "Gamma dinamica, dB",
    "start": "Avvia",
    "stop": "Ferma",
    "dwell": "Permanenza per frequenza",
    "dwell_single": "Cattura singola",
    "sweep": "Tempo di scansione",
    "sweep_every": "Ogni scansione",
    "scan_networks": "Cerca reti",
    "scanning_networks": "Ricerca…",
    "hidden_network": "Rete nascosta",
    "unit_mhz": "MHz",
    "unit_dbm": "dBm",
    "value_ms": "{value} ms",
    "value_s": "{value} s",
    "error_span_integer": "Inserisci le frequenze in MHz interi",
    "error_span_limits": "Le frequenze devono essere comprese tra {min} e {max} MHz",
    "error_span_order": "La frequenza iniziale deve essere inferiore a quella finale",
    "error_info": "Impossibile leggere i parametri del ricevitore",
    "error_capture": "Impossibile ottenere lo spettro dal dispositivo",
    "error_networks": "Impossibile cercare le reti Wi-Fi",
    "error_rejected": "Il dispositivo ha rifiutato i parametri di scansione",
    "error_timeout": "Il dispositivo non ha risposto in tempo",
    "error_unauthorized": "La sessione è scaduta, accedi di nuovo"
  },
  "de": {
    "title": "SDR-Bandscanner",
    "crumbs": "Spektrum-Sweep und Wasserfall mit WLAN-Kanalmarken",
    "receiver": "Empfänger",
    "display": "Anzeige",
    "plots": "Spektrum und Wasserfall",
    "spectrum": "Spektrum",
    "waterfall": "Wasserfall",
    "start_freq": "Startfrequenz, MHz",
    "stop_freq": "Stoppfrequenz, MHz",
    "gain": "Verstärkung",
    "agc": "AGC",
    "agc_aria": "Automatische Verstärkungsregelung",
    "floor": "Untergrenze, dBFS",
    "range": "Dynamikbereich, dB",
    "start": "Starten",
    "stop": "Stoppen",
    "dwell": "Verweildauer pro Frequenz",
    "dwell_single": "Einzelerfassung",
    "sweep": "Sweep-Zeit",
    "sweep_every": "Jeder Scan",
    "scan_networks": "Netzwerke suchen",
    "scanning_networks": "Suche…",
    "hidden_network": "Verborgenes Netzwerk",
    "unit_mhz": "MHz",
    "unit_dbm": "dBm",
    "value_ms": "{value} ms",
    "value_s": "{value} s",
    "error_span_integer": "Frequenzen in ganzen MHz angeben",
    "error_span_limits": "Die Frequenzen müssen zwischen {min} und {max} MHz liegen",
    "error_span_order": "Die Startfrequenz muss unter der Stoppfrequenz liegen",
    "error_info": "Die Empfängerparameter konnten nicht gelesen werden",
    "error_capture": "Vom Gerät konnte kein Spektrum empfangen werden",
    "error_networks": "WLAN-Netzwerke konnten nicht gesucht werden",
    "error_rejected": "Das Gerät hat die Scan-Parameter abgelehnt",
    "error_timeout": "Das Gerät hat nicht rechtzeitig geantwortet",
    "error_unauthorized": "Die Sitzung ist abgelaufen, bitte erneut anmelden"
  }
}
</i18n>
