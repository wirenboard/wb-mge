<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref } from 'vue';

// A component of its own so that only these few nodes re-render every animation frame, not the page.
const props = defineProps<{
  // Device time, unix ms, at the performance.now() instant `perfMs`; null while it is not known.
  unixMs: number | null;
  perfMs: number;
  caption: string;
}>();

const DASH = '—';
const MS_WIDTH = 3;

const now = ref(performance.now());
let frame = 0;

const tick = () => {
  now.value = performance.now();
  frame = requestAnimationFrame(tick);
};

onMounted(() => {
  frame = requestAnimationFrame(tick);
});

onBeforeUnmount(() => {
  cancelAnimationFrame(frame);
});

const pad = (value: number, width = 2) => String(value).padStart(width, '0');

const current = computed(() => {
  if (props.unixMs === null) {
    return null;
  }
  const date = new Date(Math.floor(props.unixMs + (now.value - props.perfMs)));
  return Number.isNaN(date.getTime()) ? null : date;
});

const time = computed(() => {
  const date = current.value;
  if (!date) {
    return DASH;
  }
  return `${pad(date.getUTCHours())}:${pad(date.getUTCMinutes())}:${pad(date.getUTCSeconds())}`
    + `.${pad(date.getUTCMilliseconds(), MS_WIDTH)}`;
});

const day = computed(() => {
  const date = current.value;
  return date ? `${date.getUTCFullYear()}-${pad(date.getUTCMonth() + 1)}-${pad(date.getUTCDate())}` : '';
});
</script>

<template>
  <div class="gpsClock">
    <div class="gpsClock-caption">{{ caption }}</div>
    <div class="gpsClock-time">{{ time }}</div>
    <div v-if="current" class="gpsClock-date">{{ day }}</div>
  </div>
</template>

<style scoped>
.gpsClock {
  padding: 10px 0 12px;
  border-bottom: 1px dashed var(--border-color);
}

.gpsClock-caption {
  font-size: 11px;
  color: var(--text-muted);
}

.gpsClock-time {
  font-family: var(--font-mono);
  font-size: 30px;
  font-weight: 600;
  font-variant-numeric: tabular-nums;
  letter-spacing: 0.01em;
  margin-top: 2px;
}

.gpsClock-date {
  font-family: var(--font-mono);
  font-size: 13px;
  color: var(--text-secondary);
}
</style>
