<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { spectrumDbfs } from '@/common/sdrSpectrum';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';
import { api, apiBinary } from '@/utils/api';

interface SdrInfo {
  gain_max: number;
  samples: number;
  rates_msps: number[];
  freq_min_mhz: number;
  freq_max_mhz: number;
  bw_min_mhz: number;
  bw_max_mhz: number;
}

// Query of GET /sdr/capture. A type alias rather than an interface, so it passes as ky's searchParams.
type CaptureParams = {
  freq: number;
  rate: number;
  bw: number;
  gain: number;
};

interface Frame {
  dbfs: Float32Array;
  params: CaptureParams;
}

const DEFAULT_FREQ_MHZ = 2412;
const DEFAULT_RATE_MSPS = 80;
const DEFAULT_BW_MHZ = 67;
const BW_AUTO = 0;
const GAIN_AGC = -1;
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
const AXIS_HEIGHT_PX = 18;

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
const freqMhz = ref(DEFAULT_FREQ_MHZ);
const rateMsps = ref(DEFAULT_RATE_MSPS);
const bwAuto = ref(false);
const bwMhz = ref(DEFAULT_BW_MHZ);
const agc = ref(true);
const gain = ref(0);
const floorDb = ref(DEFAULT_FLOOR_DB);
const rangeDb = ref(DEFAULT_RANGE_DB);
const isRunning = ref(false);
const errorKey = ref('');

const spectrumCanvas = ref<HTMLCanvasElement>();
const waterfallCanvas = ref<HTMLCanvasElement>();

let lastFrame: Frame | null = null;
let loop: Promise<void> | null = null;
let resizeObserver: ResizeObserver | null = null;

const captureParams = (): CaptureParams => ({
  freq: freqMhz.value,
  rate: rateMsps.value,
  bw: bwAuto.value ? BW_AUTO : bwMhz.value,
  gain: agc.value ? GAIN_AGC : gain.value,
});

// Frequency, rate and bandwidth decide what a waterfall column means; gain only changes its level.
const tuningOf = (params: CaptureParams): string => `${params.freq}/${params.rate}/${params.bw}`;
const tuning = computed(() => tuningOf(captureParams()));

// api() and apiBinary() already turn a timeout and a lost session into plain messages; a 400 is the
// device refusing the capture parameters and still carries ky's response.
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

  if (!lastFrame) {
    return;
  }
  const { dbfs, params } = lastFrame;
  ctx.strokeStyle = TRACE_COLOR;
  ctx.beginPath();
  for (let bin = 0; bin < dbfs.length; bin++) {
    const x = ((bin + 0.5) * width) / dbfs.length;
    const y = Math.min(plotHeight, Math.max(0, yOf(dbfs[bin])));
    if (bin === 0) {
      ctx.moveTo(x, y);
    } else {
      ctx.lineTo(x, y);
    }
  }
  ctx.stroke();

  const halfSpan = params.rate / 2;
  const labels: Array<[number, CanvasTextAlign, number]> = [
    [LABEL_PAD_PX, 'left', params.freq - halfSpan],
    [width / 2, 'center', params.freq],
    [width - LABEL_PAD_PX, 'right', params.freq + halfSpan],
  ];
  ctx.fillStyle = LABEL_COLOR;
  ctx.textBaseline = 'middle';
  for (const [x, align, mhz] of labels) {
    ctx.textAlign = align;
    ctx.fillText(`${mhz} ${t('unit_mhz')}`, x, plotHeight + (AXIS_HEIGHT_PX / 2));
  }
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

const paintWaterfallRow = (dbfs: Float32Array) => {
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
  const bins = dbfs.length;
  for (let x = 0; x < width; x++) {
    // Pixel x covers bins [first, last); on a canvas wider than the FFT it still takes one bin.
    const first = Math.floor((x * bins) / width);
    const last = Math.max(first + 1, Math.floor(((x + 1) * bins) / width));
    let peak = dbfs[first];
    for (let bin = first + 1; bin < last; bin++) {
      peak = Math.max(peak, dbfs[bin]);
    }
    const level = Math.min(1, Math.max(0, (peak - floor) / range));
    const color = Math.round(level * (LUT_SIZE - 1)) * 3;
    const pixel = x * 4;
    row.data[pixel] = WATERFALL_LUT[color];
    row.data[pixel + 1] = WATERFALL_LUT[color + 1];
    row.data[pixel + 2] = WATERFALL_LUT[color + 2];
    row.data[pixel + 3] = 255;
  }
  ctx.putImageData(row, 0, 0);
};

const captureLoop = async () => {
  while (isRunning.value) {
    const params = captureParams();
    try {
      const buffer = await apiBinary('sdr/capture', { searchParams: params });
      // A capture tuned before the last frequency, rate or bandwidth change would paint a stale row
      // onto the waterfall that was just cleared for the new tuning.
      if (isRunning.value && (tuningOf(params) === tuning.value)) {
        lastFrame = { dbfs: spectrumDbfs(buffer), params };
        drawSpectrum();
        paintWaterfallRow(lastFrame.dbfs);
      }
    } catch (err) {
      if (isRunning.value) {
        console.error('SDR capture failed', err);
        errorKey.value = errorKeyFor(err, 'error_capture');
        isRunning.value = false;
      }
    }
  }
};

const start = () => {
  errorKey.value = '';
  isRunning.value = true;
  // After Stop the previous loop may still be waiting for its last capture; it then simply carries
  // on, so a quick Stop/Start never puts two requests in flight.
  if (!loop) {
    loop = captureLoop().finally(() => {
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

watch(tuning, () => clearWaterfall());
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
    gain.value = Math.round(sdrInfo.gain_max / 2);
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
              :disabled="!info"
              @click="isRunning ? stop() : start()"
            >
              {{ isRunning ? t('stop') : t('start') }}
            </Button>
          </div>
          <div class="card-body sdr-controls">
            <div class="field">
              <label for="sdr_freq">{{ t('center_freq') }}</label>
              <input
                id="sdr_freq"
                v-model.lazy.number="freqMhz"
                type="number"
                step="1"
                :min="info?.freq_min_mhz"
                :max="info?.freq_max_mhz"
              >
            </div>
            <div class="field">
              <label for="sdr_rate">{{ t('sample_rate') }}</label>
              <select id="sdr_rate" v-model="rateMsps">
                <option v-for="rate in info?.rates_msps" :key="rate" :value="rate">{{ rate }}</option>
              </select>
            </div>
            <div class="field">
              <label for="sdr_bw">{{ t('bandwidth') }}</label>
              <div class="sdr-inline">
                <Switch id="sdr_bw_auto" v-model="bwAuto" :aria-label="t('bw_auto_aria')" />
                <span class="sdr-caption">{{ t('bw_auto') }}</span>
                <input
                  id="sdr_bw"
                  v-model.lazy.number="bwMhz"
                  type="number"
                  step="1"
                  :min="info?.bw_min_mhz"
                  :max="info?.bw_max_mhz"
                  :disabled="bwAuto"
                >
              </div>
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

.sdr-inline input[type="number"] {
  flex: 1;
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
    "title": "SDR receiver",
    "crumbs": "Live spectrum and waterfall",
    "receiver": "Receiver",
    "display": "Display",
    "plots": "Spectrum and waterfall",
    "spectrum": "Spectrum",
    "waterfall": "Waterfall",
    "center_freq": "Center frequency, MHz",
    "sample_rate": "Sample rate, Msps",
    "bandwidth": "Filter bandwidth, MHz",
    "bw_auto": "Auto",
    "bw_auto_aria": "Automatic filter bandwidth",
    "gain": "Gain",
    "agc": "AGC",
    "agc_aria": "Automatic gain control",
    "floor": "Display floor, dBFS",
    "range": "Dynamic range, dB",
    "start": "Start",
    "stop": "Stop",
    "unit_mhz": "MHz",
    "error_info": "Could not read the receiver parameters from the device",
    "error_capture": "Could not get samples from the device",
    "error_rejected": "The device rejected the capture parameters",
    "error_timeout": "The device did not answer in time",
    "error_unauthorized": "The session has expired, log in again"
  },
  "ru": {
    "title": "SDR-приёмник",
    "crumbs": "Спектр и водопад в реальном времени",
    "receiver": "Приёмник",
    "display": "Отображение",
    "plots": "Спектр и водопад",
    "spectrum": "Спектр",
    "waterfall": "Водопад",
    "center_freq": "Центральная частота, МГц",
    "sample_rate": "Частота дискретизации, Мвыб/с",
    "bandwidth": "Полоса фильтра, МГц",
    "bw_auto": "Авто",
    "bw_auto_aria": "Автоматическая полоса фильтра",
    "gain": "Усиление",
    "agc": "АРУ",
    "agc_aria": "Автоматическая регулировка усиления",
    "floor": "Нижний уровень, дБFS",
    "range": "Динамический диапазон, дБ",
    "start": "Запустить",
    "stop": "Остановить",
    "unit_mhz": "МГц",
    "error_info": "Не удалось получить параметры приёмника",
    "error_capture": "Не удалось получить отсчёты с устройства",
    "error_rejected": "Устройство отклонило параметры захвата",
    "error_timeout": "Устройство не ответило вовремя",
    "error_unauthorized": "Сессия истекла, войдите снова"
  },
  "kk": {
    "title": "SDR қабылдағышы",
    "crumbs": "Нақты уақыттағы спектр және сарқырама",
    "receiver": "Қабылдағыш",
    "display": "Көрсету",
    "plots": "Спектр және сарқырама",
    "spectrum": "Спектр",
    "waterfall": "Сарқырама",
    "center_freq": "Орталық жиілік, МГц",
    "sample_rate": "Дискреттеу жиілігі, Msps",
    "bandwidth": "Сүзгі жолағы, МГц",
    "bw_auto": "Авто",
    "bw_auto_aria": "Сүзгі жолағын автоматты таңдау",
    "gain": "Күшейту",
    "agc": "AGC",
    "agc_aria": "Күшейтуді автоматты реттеу",
    "floor": "Төменгі деңгей, dBFS",
    "range": "Динамикалық диапазон, dB",
    "start": "Іске қосу",
    "stop": "Тоқтату",
    "unit_mhz": "МГц",
    "error_info": "Қабылдағыш параметрлерін алу мүмкін болмады",
    "error_capture": "Құрылғыдан сигнал үлгілерін алу мүмкін болмады",
    "error_rejected": "Құрылғы түсіру параметрлерін қабылдамады",
    "error_timeout": "Құрылғы уақытында жауап бермеді",
    "error_unauthorized": "Сеанс мерзімі аяқталды, қайта кіріңіз"
  },
  "it": {
    "title": "Ricevitore SDR",
    "crumbs": "Spettro e waterfall in tempo reale",
    "receiver": "Ricevitore",
    "display": "Visualizzazione",
    "plots": "Spettro e waterfall",
    "spectrum": "Spettro",
    "waterfall": "Waterfall",
    "center_freq": "Frequenza centrale, MHz",
    "sample_rate": "Frequenza di campionamento, Msps",
    "bandwidth": "Larghezza di banda del filtro, MHz",
    "bw_auto": "Auto",
    "bw_auto_aria": "Larghezza di banda automatica",
    "gain": "Guadagno",
    "agc": "AGC",
    "agc_aria": "Controllo automatico del guadagno",
    "floor": "Livello minimo, dBFS",
    "range": "Gamma dinamica, dB",
    "start": "Avvia",
    "stop": "Ferma",
    "unit_mhz": "MHz",
    "error_info": "Impossibile leggere i parametri del ricevitore",
    "error_capture": "Impossibile acquisire i campioni dal dispositivo",
    "error_rejected": "Il dispositivo ha rifiutato i parametri di acquisizione",
    "error_timeout": "Il dispositivo non ha risposto in tempo",
    "error_unauthorized": "La sessione è scaduta, accedi di nuovo"
  },
  "de": {
    "title": "SDR-Empfänger",
    "crumbs": "Live-Spektrum und Wasserfall",
    "receiver": "Empfänger",
    "display": "Anzeige",
    "plots": "Spektrum und Wasserfall",
    "spectrum": "Spektrum",
    "waterfall": "Wasserfall",
    "center_freq": "Mittenfrequenz, MHz",
    "sample_rate": "Abtastrate, MS/s",
    "bandwidth": "Filterbandbreite, MHz",
    "bw_auto": "Auto",
    "bw_auto_aria": "Automatische Filterbandbreite",
    "gain": "Verstärkung",
    "agc": "AGC",
    "agc_aria": "Automatische Verstärkungsregelung",
    "floor": "Untergrenze, dBFS",
    "range": "Dynamikbereich, dB",
    "start": "Starten",
    "stop": "Stoppen",
    "unit_mhz": "MHz",
    "error_info": "Die Empfängerparameter konnten nicht gelesen werden",
    "error_capture": "Vom Gerät konnten keine Abtastwerte empfangen werden",
    "error_rejected": "Das Gerät hat die Erfassungsparameter abgelehnt",
    "error_timeout": "Das Gerät hat nicht rechtzeitig geantwortet",
    "error_unauthorized": "Die Sitzung ist abgelaufen, bitte erneut anmelden"
  }
}
</i18n>
