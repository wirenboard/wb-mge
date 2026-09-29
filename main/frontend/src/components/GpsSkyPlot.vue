<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import {
  type SkyDetail,
  type SkyPoint,
  type SkySatellite,
  type SnrBand,
  skyHitTest,
  skyPolar,
  skyProject,
  skyRingRadius,
  skyTooltipOrigin,
  snrBand,
} from '@/common/skyPlot';

// A canvas rather than SVG markup: the frontend keeps no inline SVG in templates.
const props = defineProps<{
  satellites: SkySatellite[];
  degreeLabel: (value: number) => string;
}>();

const FULL_TURN_DEG = 360;
const AZIMUTH_STEP_DEG = 30;
const ELEVATION_RINGS_DEG = [0, 30, 60];
// Between two spokes, so no line runs through the elevation labels.
const ELEVATION_LABEL_AZIMUTH_DEG = 45;
const ELEVATION_LABEL_PADDING = 2;
const ELEVATION_LABEL_HEIGHT = 12;
// Room outside the horizon circle for the azimuth labels.
const PLOT_MARGIN = 30;
const AZIMUTH_LABEL_OFFSET = 14;
const GRID_LINE = 1;
const MARKER_RADIUS = 11;
const MARKER_OUTLINE = 2;
const HOVER_GROW = 2;
const HOVER_RING_GAP = 3;
const HIT_SLACK = 3;
const UNUSED_FILL_ALPHA = 0.2;
const TOOLTIP_GAP = MARKER_RADIUS + 8;
const TOOLTIP_MARGIN = 2;
const TOOLTIP_PADDING = 8;
const TOOLTIP_ROW = 17;
const TOOLTIP_COLUMN_GAP = 14;
const TOOLTIP_RADIUS = 6;
// Half a device-independent pixel puts a 1 px line on a pixel row instead of across two.
const HALF_PIXEL = 0.5;

interface Palette {
  disc: string;
  grid: string;
  horizon: string;
  label: string;
  text: string;
  textSecondary: string;
  surface: string;
  border: string;
  bands: Record<SnrBand, string>;
  fontUi: string;
}

const canvas = ref<HTMLCanvasElement>();
let pointer: SkyPoint | null = null;
let frame = 0;
let resizeObserver: ResizeObserver | null = null;

// Read on every draw, so the plot always matches the stylesheet.
const readPalette = (element: HTMLElement): Palette => {
  const style = getComputedStyle(element);
  const token = (name: string) => style.getPropertyValue(name).trim();
  return {
    disc: token('--bg-surface-subtle'),
    grid: token('--border-color'),
    horizon: token('--border-strong'),
    label: token('--text-muted'),
    text: token('--text-color'),
    textSecondary: token('--text-secondary'),
    surface: token('--bg-surface'),
    border: token('--border-strong'),
    bands: {
      strong: token('--snr-strong'),
      medium: token('--snr-medium'),
      weak: token('--snr-weak'),
      none: token('--snr-none'),
    },
    fontUi: token('--font-ui'),
  };
};

const drawGrid = (ctx: CanvasRenderingContext2D, palette: Palette, centre: SkyPoint, radius: number) => {
  ctx.fillStyle = palette.disc;
  ctx.beginPath();
  ctx.arc(centre.x, centre.y, radius, 0, 2 * Math.PI);
  ctx.fill();

  ctx.lineWidth = GRID_LINE;
  ctx.strokeStyle = palette.grid;
  ctx.beginPath();
  for (let azimuth = 0; azimuth < FULL_TURN_DEG; azimuth += AZIMUTH_STEP_DEG) {
    const end = skyPolar(azimuth, radius, centre);
    ctx.moveTo(centre.x, centre.y);
    ctx.lineTo(end.x, end.y);
  }
  ctx.stroke();

  for (const elevation of ELEVATION_RINGS_DEG) {
    ctx.strokeStyle = elevation === 0 ? palette.horizon : palette.grid;
    ctx.beginPath();
    ctx.arc(centre.x, centre.y, skyRingRadius(elevation, radius), 0, 2 * Math.PI);
    ctx.stroke();
  }

  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.font = `10px ${palette.fontUi}`;
  for (const elevation of ELEVATION_RINGS_DEG) {
    const text = props.degreeLabel(elevation);
    const at = skyPolar(ELEVATION_LABEL_AZIMUTH_DEG, skyRingRadius(elevation, radius), centre);
    const knockoutWidth = ctx.measureText(text).width + (2 * ELEVATION_LABEL_PADDING);
    // Sits on its ring, the line cut away behind it.
    ctx.fillStyle = palette.disc;
    ctx.fillRect(at.x - (knockoutWidth / 2), at.y - (ELEVATION_LABEL_HEIGHT / 2), knockoutWidth, ELEVATION_LABEL_HEIGHT);
    ctx.fillStyle = palette.label;
    ctx.fillText(text, at.x, at.y);
  }

  ctx.fillStyle = palette.label;
  ctx.font = `11px ${palette.fontUi}`;
  for (let azimuth = 0; azimuth < FULL_TURN_DEG; azimuth += AZIMUTH_STEP_DEG) {
    const at = skyPolar(azimuth, radius + AZIMUTH_LABEL_OFFSET, centre);
    ctx.fillText(props.degreeLabel(azimuth), at.x, at.y);
  }
};

// Used in the fix: a solid disc. Not used: a tinted ring, so the two differ in shape, not only in colour.
const drawMarker = (
  ctx: CanvasRenderingContext2D,
  palette: Palette,
  sat: SkySatellite,
  at: SkyPoint,
  hovered: boolean,
) => {
  const colour = palette.bands[snrBand(sat.snr)];
  const radius = hovered ? MARKER_RADIUS + HOVER_GROW : MARKER_RADIUS;
  ctx.beginPath();
  ctx.arc(at.x, at.y, radius, 0, 2 * Math.PI);
  if (sat.used) {
    ctx.fillStyle = colour;
    ctx.fill();
  } else {
    ctx.fillStyle = palette.surface;
    ctx.fill();
    ctx.globalAlpha = UNUSED_FILL_ALPHA;
    ctx.fillStyle = colour;
    ctx.fill();
    ctx.globalAlpha = 1;
    ctx.lineWidth = MARKER_OUTLINE;
    ctx.strokeStyle = colour;
    ctx.stroke();
  }
  if (hovered) {
    ctx.beginPath();
    ctx.arc(at.x, at.y, radius + HOVER_RING_GAP, 0, 2 * Math.PI);
    ctx.lineWidth = MARKER_OUTLINE;
    ctx.strokeStyle = palette.text;
    ctx.stroke();
  }
  ctx.fillStyle = sat.used ? palette.text : palette.textSecondary;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.font = `600 10px ${palette.fontUi}`;
  ctx.fillText(String(sat.prn), at.x, at.y);
};

const drawTooltip = (
  ctx: CanvasRenderingContext2D,
  palette: Palette,
  details: SkyDetail[],
  anchor: SkyPoint,
  bounds: { width: number; height: number },
) => {
  const labelFont = `11px ${palette.fontUi}`;
  const valueFont = `600 11px ${palette.fontUi}`;
  ctx.font = labelFont;
  const labelWidth = Math.max(0, ...details.map((row) => ctx.measureText(row.label).width));
  ctx.font = valueFont;
  const valueWidth = Math.max(0, ...details.map((row) => ctx.measureText(row.value).width));
  const size = {
    width: Math.ceil((2 * TOOLTIP_PADDING) + labelWidth + TOOLTIP_COLUMN_GAP + valueWidth),
    height: (2 * TOOLTIP_PADDING) + (details.length * TOOLTIP_ROW),
  };
  const origin = skyTooltipOrigin(anchor, size, bounds, TOOLTIP_GAP, TOOLTIP_MARGIN);
  const left = Math.round(origin.x);
  const top = Math.round(origin.y);

  ctx.beginPath();
  ctx.roundRect(left + HALF_PIXEL, top + HALF_PIXEL, size.width, size.height, TOOLTIP_RADIUS);
  ctx.fillStyle = palette.surface;
  ctx.fill();
  ctx.lineWidth = GRID_LINE;
  ctx.strokeStyle = palette.border;
  ctx.stroke();

  ctx.textBaseline = 'middle';
  details.forEach((row, index) => {
    const y = top + TOOLTIP_PADDING + (index * TOOLTIP_ROW) + (TOOLTIP_ROW / 2);
    ctx.font = labelFont;
    ctx.fillStyle = palette.label;
    ctx.textAlign = 'left';
    ctx.fillText(row.label, left + TOOLTIP_PADDING, y);
    ctx.font = valueFont;
    ctx.fillStyle = palette.text;
    ctx.textAlign = 'right';
    ctx.fillText(row.value, left + size.width - TOOLTIP_PADDING, y);
  });
};

const draw = () => {
  frame = 0;
  const element = canvas.value;
  const ctx = element?.getContext('2d');
  if (!element || !ctx) {
    return;
  }
  const width = element.clientWidth;
  const height = element.clientHeight;
  if ((width <= 0) || (height <= 0)) {
    return;
  }
  // The backing store follows the CSS size times devicePixelRatio, so the plot stays sharp on
  // HiDPI screens; it is checked on every draw, which also catches a zoom that did not resize it.
  const scale = window.devicePixelRatio || 1;
  const pixelWidth = Math.round(width * scale);
  const pixelHeight = Math.round(height * scale);
  if ((element.width !== pixelWidth) || (element.height !== pixelHeight)) {
    element.width = pixelWidth;
    element.height = pixelHeight;
  }
  ctx.setTransform(pixelWidth / width, 0, 0, pixelHeight / height, 0, 0);
  ctx.clearRect(0, 0, width, height);

  const palette = readPalette(element);
  const centre = { x: width / 2, y: height / 2 };
  const radius = Math.max(0, (Math.min(width, height) / 2) - PLOT_MARGIN);
  drawGrid(ctx, palette, centre, radius);

  const points = props.satellites.map((sat) => skyProject(sat.elevation, sat.azimuth, centre, radius));
  const hovered = pointer ? skyHitTest(points, pointer, MARKER_RADIUS + HIT_SLACK) : -1;
  props.satellites.forEach((sat, index) => {
    if (index !== hovered) {
      drawMarker(ctx, palette, sat, points[index], false);
    }
  });
  if (hovered >= 0) {
    drawMarker(ctx, palette, props.satellites[hovered], points[hovered], true);
    drawTooltip(ctx, palette, props.satellites[hovered].details, points[hovered], { width, height });
  }
};

const scheduleDraw = () => {
  if (!frame) {
    frame = requestAnimationFrame(draw);
  }
};

const onMouseMove = (event: MouseEvent) => {
  pointer = { x: event.offsetX, y: event.offsetY };
  scheduleDraw();
};

const onMouseLeave = () => {
  pointer = null;
  scheduleDraw();
};

watch(() => props.satellites, scheduleDraw);

onMounted(() => {
  resizeObserver = new ResizeObserver(scheduleDraw);
  if (canvas.value) {
    resizeObserver.observe(canvas.value);
  }
  // Labels drawn before the web font arrived would stay in the fallback font until the next redraw.
  document.fonts.ready.then(scheduleDraw);
});

onBeforeUnmount(() => {
  resizeObserver?.disconnect();
  cancelAnimationFrame(frame);
  frame = 0;
});
</script>

<template>
  <canvas ref="canvas" class="gpsSkyPlot" @mousemove="onMouseMove" @mouseleave="onMouseLeave" />
</template>

<style scoped>
.gpsSkyPlot {
  display: block;
  width: 100%;
  aspect-ratio: 1;
}
</style>
