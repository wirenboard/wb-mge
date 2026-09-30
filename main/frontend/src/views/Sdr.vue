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
}

// Query of GET /sdr/scan. A type alias rather than an interface, so it passes as ky's searchParams.
type ScanParams = {
  gain: number;
};

// A whole device scan: bin i is centred at startMhz + i * binMhz.
interface Frame {
  dbfs: Int8Array;
  startMhz: number;
  binMhz: number;
}

interface WifiChannel {
  number: number;
  mhz: number;
}

const DEFAULT_START_MHZ = 2400;
const DEFAULT_STOP_MHZ = 2500;
const DEFAULT_GAIN = 60;
const GAIN_AGC = -1;
const KHZ_PER_MHZ = 1000;
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

const WIFI_CHANNEL_1_MHZ = 2412;
const WIFI_CHANNEL_STEP_MHZ = 5;
const WIFI_REGULAR_CHANNELS = 13;
const WIFI_CHANNEL_14_MHZ = 2484;
const WIFI_CHANNELS: ReadonlyArray<WifiChannel> = [
  ...Array.from({ length: WIFI_REGULAR_CHANNELS }, (_, index) => ({
    number: index + 1,
    mhz: WIFI_CHANNEL_1_MHZ + (WIFI_CHANNEL_STEP_MHZ * index),
  })),
  { number: WIFI_REGULAR_CHANNELS + 1, mhz: WIFI_CHANNEL_14_MHZ },
];

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

const { t } = useI18n();

const info = ref<SdrInfo>();
// The fields hold what was typed; the plots show the last pair that passed validation.
const startMhz = ref(DEFAULT_START_MHZ);
const stopMhz = ref(DEFAULT_STOP_MHZ);
const scanStartMhz = ref(DEFAULT_START_MHZ);
const scanStopMhz = ref(DEFAULT_STOP_MHZ);
// With AGC every sweep step picks its own gain and the stitched spectrum shows steps, hence manual by default.
const agc = ref(false);
const gain = ref(DEFAULT_GAIN);
const floorDb = ref(DEFAULT_FLOOR_DB);
const rangeDb = ref(DEFAULT_RANGE_DB);
const isRunning = ref(false);
const errorKey = ref('');

const spectrumCanvas = ref<HTMLCanvasElement>();
const waterfallCanvas = ref<HTMLCanvasElement>();

let lastFrame: Frame | null = null;
let loop: Promise<void> | null = null;
let resizeObserver: ResizeObserver | null = null;

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
});

const isSameScan = (a: ScanParams, b: ScanParams): boolean => a.gain === b.gain;

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

const scanLoop = async () => {
  while (isRunning.value && info.value) {
    const limits = info.value;
    const params = scanParams();
    try {
      const buffer = await apiBinary('sdr/scan', { searchParams: params });
      // A sweep requested before the last gain change no longer matches the controls.
      if (!isRunning.value || !isSameScan(params, scanParams())) {
        continue;
      }
      const dbfs = new Int8Array(buffer);
      const expected = expectedBins(limits);
      // The device ends the body early when a capture fails mid-sweep.
      if (dbfs.length !== expected) {
        throw new Error(`SDR scan returned ${dbfs.length} bins instead of ${expected}`);
      }
      lastFrame = { dbfs, startMhz: limits.start_mhz, binMhz: limits.bin_khz / KHZ_PER_MHZ };
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

const onResize = () => {
  if (spectrumCanvas.value && fitCanvas(spectrumCanvas.value)) {
    drawSpectrum();
  }
  if (waterfallCanvas.value && fitCanvas(waterfallCanvas.value)) {
    clearWaterfall();
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
});
watch([floorDb, rangeDb], () => drawSpectrum());

onMounted(async () => {
  resizeObserver = new ResizeObserver(onResize);
  for (const canvas of [spectrumCanvas.value, waterfallCanvas.value]) {
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
            <Button
              type="button"
              :variant="isRunning ? 'outline' : 'primary'"
              :disabled="!info || (!isRunning && !!spanError)"
              @click="isRunning ? stop() : start()"
            >
              {{ isRunning ? t('stop') : t('start') }}
            </Button>
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
            <div v-if="spanError" class="sdr-error" role="alert">{{ spanError }}</div>
            <div v-if="errorKey" class="sdr-error" role="alert">{{ t(errorKey) }}</div>
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
          </div>
        </section>
      </div>

      <section class="card">
        <div class="card-header">
          <div class="title">{{ t('plots') }}</div>
        </div>
        <div class="card-body sdr-plots">
          <canvas ref="spectrumCanvas" class="sdr-spectrum" role="img" :aria-label="t('spectrum')" />
          <canvas ref="waterfallCanvas" class="sdr-waterfall" role="img" :aria-label="t('waterfall')" />
        </div>
      </section>
    </div>
  </Layout>
</template>

<style scoped>
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

.sdr-spectrum,
.sdr-waterfall {
  display: block;
  width: 100%;
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
    "unit_mhz": "MHz",
    "error_span_integer": "Enter the frequencies in whole MHz",
    "error_span_limits": "The frequencies must lie within {min}–{max} MHz",
    "error_span_order": "The start frequency must be below the stop frequency",
    "error_info": "Could not read the receiver parameters from the device",
    "error_capture": "Could not get the spectrum from the device",
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
    "unit_mhz": "МГц",
    "error_span_integer": "Укажите частоты целым числом МГц",
    "error_span_limits": "Частоты должны быть в пределах {min}–{max} МГц",
    "error_span_order": "Начальная частота должна быть ниже конечной",
    "error_info": "Не удалось получить параметры приёмника",
    "error_capture": "Не удалось получить спектр с устройства",
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
    "unit_mhz": "МГц",
    "error_span_integer": "Жиіліктерді бүтін МГц санымен енгізіңіз",
    "error_span_limits": "Жиіліктер {min}–{max} МГц аралығында болуы керек",
    "error_span_order": "Бастапқы жиілік соңғы жиіліктен төмен болуы керек",
    "error_info": "Қабылдағыш параметрлерін алу мүмкін болмады",
    "error_capture": "Құрылғыдан спектрді алу мүмкін болмады",
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
    "unit_mhz": "MHz",
    "error_span_integer": "Inserisci le frequenze in MHz interi",
    "error_span_limits": "Le frequenze devono essere comprese tra {min} e {max} MHz",
    "error_span_order": "La frequenza iniziale deve essere inferiore a quella finale",
    "error_info": "Impossibile leggere i parametri del ricevitore",
    "error_capture": "Impossibile ottenere lo spettro dal dispositivo",
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
    "unit_mhz": "MHz",
    "error_span_integer": "Frequenzen in ganzen MHz angeben",
    "error_span_limits": "Die Frequenzen müssen zwischen {min} und {max} MHz liegen",
    "error_span_order": "Die Startfrequenz muss unter der Stoppfrequenz liegen",
    "error_info": "Die Empfängerparameter konnten nicht gelesen werden",
    "error_capture": "Vom Gerät konnte kein Spektrum empfangen werden",
    "error_rejected": "Das Gerät hat die Scan-Parameter abgelehnt",
    "error_timeout": "Das Gerät hat nicht rechtzeitig geantwortet",
    "error_unauthorized": "Die Sitzung ist abgelaufen, bitte erneut anmelden"
  }
}
</i18n>
