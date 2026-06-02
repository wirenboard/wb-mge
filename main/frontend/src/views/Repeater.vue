<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import { useInfo } from '@/common/info';
import { useAlerts } from '@/common/alert';
import { useOptimisticToggle } from '@/common/useOptimisticToggle';
import { api } from '@/utils/api';
import { avgBytesPerSec as computeAvgBytesPerSec, groupBytes, formatUptime, lineParams } from '@/views/repeaterFormat';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';

const { t } = useI18n();
const { initData: savedSettings } = useSettings();
const { info } = useInfo();
const { showAlert } = useAlerts();

// A SINGLE optimistic toggle for the whole repeater. The repeater is considered ON only when
// BOTH ports are in 'repeater' mode; on a failed toggle we surface the connection alert.
const toggle = useOptimisticToggle({
  derive: () =>
    info.value?.rs485_1.port_mode === 'repeater' &&
    info.value?.rs485_2.port_mode === 'repeater',
  onError: () => showAlert('connection_error'),
});

// The displayed enable state (optimistic override if set, else derived from info).
const isEnabled = computed<boolean>(() => toggle.value.value);

// True while a toggle request is in flight or info has not loaded yet.
const isToggleDisabled = computed<boolean>(() => toggle.inFlight.value || info.value === undefined);

// Toggle the repeater. ON -> set BOTH ports to 'repeater'. OFF -> set BOTH ports to 'disabled'.
function toggleRepeater(): void {
  if (info.value === undefined) return; // cannot determine target state yet
  toggle.run(async (wasEnabled) => {
    const mode = wasEnabled ? 'disabled' : 'repeater';
    await Promise.all([
      api<void>('ports/1/mode', { method: 'POST', json: { mode } }),
      api<void>('ports/2/mode', { method: 'POST', json: { mode } }),
    ]);
  });
}

// ---------------------------------------------------------------------------
// Live stats from info.repeater
// ---------------------------------------------------------------------------
const forwardBytes = computed<number>(() => info.value?.repeater?.bytes_1to2 ?? 0); // Port 1 -> Port 2
const reverseBytes = computed<number>(() => info.value?.repeater?.bytes_2to1 ?? 0); // Port 2 -> Port 1
const dropped1 = computed<number>(() => info.value?.repeater?.dropped_1 ?? 0);
const dropped2 = computed<number>(() => info.value?.repeater?.dropped_2 ?? 0);
const uptimeS = computed<number>(() => info.value?.repeater?.uptime_s ?? 0);

// Average throughput (B/s) over the active uptime, rounded to 1 decimal. Computed client-side.
const avgBytesPerSec = computed<number>(() => computeAvgBytesPerSec(forwardBytes.value, reverseBytes.value, uptimeS.value));

const port1Line = computed<string>(() => lineParams(savedSettings.value?.rs485_1));
const port2Line = computed<string>(() => lineParams(savedSettings.value?.rs485_2));
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <!-- Warning: both segments behave as electrically connected -->
      <div class="rep-banner">
        <div class="rep-banner-icon">
          <svg
            width="16"
            height="16"
            viewBox="0 0 16 16"
            fill="none"
            stroke="currentColor"
            stroke-width="1.6"
            stroke-linecap="round"
            stroke-linejoin="round"
          >
            <path d="M8 2l6.5 11.5a1 1 0 0 1-.87 1.5H2.37a1 1 0 0 1-.87-1.5z" />
            <path d="M8 6.5v3.5M8 12.3v.2" />
          </svg>
        </div>
        <div>
          <div class="rep-banner-title">{{ t('warning_title') }}</div>
          <div class="rep-banner-body">{{ t('warning_body') }}</div>
        </div>
      </div>

      <!-- Bridge card: Port 1 | toggle + arrows | Port 2 -->
      <section class="card">
        <div class="card-header">
          <div class="card-title-wrap">
            <div class="title">{{ t('bridge') }}</div>
            <div class="sub">{{ t('bridge_sub') }}</div>
          </div>
        </div>

        <div class="rep-stage">
          <!-- Port 1 panel -->
          <div class="rep-port rep-port-info">
            <div class="rep-port-head">
              <span>RS-485</span>
              <span class="mono rep-port-line">{{ port1Line }}</span>
            </div>
            <div class="rep-port-body">
              <div class="rep-port-title">{{ t('port_1') }}</div>
              <dl class="rep-port-stats">
                <dt>{{ t('dropped_bytes') }}</dt>
                <dd><span class="mono v-err">{{ groupBytes(dropped1) }}</span></dd>
              </dl>
            </div>
          </div>

          <!-- Center: arrows + toggle -->
          <div class="rep-center">
            <div class="rep-flow rep-flow-fwd">
              <span class="rep-flow-tag">TX</span>
              <span class="rep-flow-tag">RX</span>
              <div class="rep-arrow rep-arrow-right">
                <span class="rep-arrow-value mono">{{ groupBytes(forwardBytes) }} <em>B</em></span>
              </div>
            </div>

            <button
              type="button"
              :class="['rep-toggle', isEnabled ? 'on' : 'off']"
              :disabled="isToggleDisabled"
              :aria-pressed="isEnabled"
              :aria-label="t('enabled')"
              @click="toggleRepeater"
            >
              <span class="rep-toggle-label">{{ isEnabled ? t('enabled') : t('disabled') }}</span>
              <div v-if="isEnabled" class="rep-toggle-meta">
                <div><span>{{ t('uptime') }}</span><b class="mono">{{ formatUptime(uptimeS) }}</b></div>
                <div><span>{{ t('avg') }}</span><b class="mono">{{ avgBytesPerSec }} <em>B/s</em></b></div>
              </div>
              <span class="rep-toggle-hint">{{ isEnabled ? t('click_to_disable') : t('click_to_enable') }}</span>
            </button>

            <div class="rep-flow rep-flow-rev">
              <span class="rep-flow-tag">RX</span>
              <span class="rep-flow-tag">TX</span>
              <div class="rep-arrow rep-arrow-left">
                <span class="rep-arrow-value mono">{{ groupBytes(reverseBytes) }} <em>B</em></span>
              </div>
            </div>
          </div>

          <!-- Port 2 panel -->
          <div class="rep-port rep-port-warn">
            <div class="rep-port-head">
              <span>RS-485</span>
              <span class="mono rep-port-line">{{ port2Line }}</span>
            </div>
            <div class="rep-port-body">
              <div class="rep-port-title">{{ t('port_2') }}</div>
              <dl class="rep-port-stats">
                <dt>{{ t('dropped_bytes') }}</dt>
                <dd><span class="mono v-err">{{ groupBytes(dropped2) }}</span></dd>
              </dl>
            </div>
          </div>
        </div>
      </section>
    </div>
  </Layout>
</template>

<style scoped>
/* Warning banner */
.rep-banner {
  display: flex;
  gap: 12px;
  align-items: flex-start;
  padding: 12px 16px;
  border: 1px solid color-mix(in oklch, var(--warn) 35%, var(--border-color));
  border-radius: var(--r-lg);
  background: color-mix(in oklch, var(--warn) 8%, var(--bg-surface));
}

.rep-banner-icon {
  flex-shrink: 0;
  color: var(--warn);
  display: flex;
  align-items: center;
  justify-content: center;
  margin-top: 1px;
}

.rep-banner-title {
  font-size: 13px;
  font-weight: 600;
  color: var(--text-color);
}

.rep-banner-body {
  font-size: 12px;
  color: var(--text-secondary);
  margin-top: 2px;
  line-height: 1.5;
}

/* Bridge stage layout: Port 1 | center | Port 2 */
.rep-stage {
  display: grid;
  grid-template-columns: 1fr minmax(220px, 280px) 1fr;
  gap: 20px;
  align-items: stretch;
  padding: 22px;

  @media (max-width: 860px) {
    grid-template-columns: 1fr;
  }
}

/* Port panels */
.rep-port {
  display: flex;
  flex-direction: column;
  border: 1px solid var(--border-color);
  border-radius: var(--r-lg);
  background: var(--bg-surface-subtle);
  overflow: hidden;
}

.rep-port-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 8px 14px;
  font-size: 10.5px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-weight: 600;
  color: var(--text-muted);
  border-bottom: 1px solid var(--border-color);
}

.rep-port-info .rep-port-head {
  color: var(--info);
  border-bottom-color: color-mix(in oklch, var(--info) 25%, var(--border-color));
}

.rep-port-warn .rep-port-head {
  color: var(--warn);
  border-bottom-color: color-mix(in oklch, var(--warn) 30%, var(--border-color));
}

.rep-port-line {
  text-transform: none;
  letter-spacing: 0.02em;
  font-weight: 500;
  color: var(--text-secondary);
}

.rep-port-body {
  padding: 14px;
  display: flex;
  flex-direction: column;
  gap: 12px;
  flex: 1;
}

.rep-port-title {
  font-family: var(--font-mono);
  font-size: clamp(30px, 3.4vw, 44px);
  line-height: 1.1;
  font-weight: 700;
  letter-spacing: 0.01em;
  color: var(--text-color);
}

/* Port titles are tinted by the port tone (Port 1 blue, Port 2 orange). */
.rep-port-info .rep-port-title {
  color: var(--info);
}

.rep-port-warn .rep-port-title {
  color: var(--warn);
}

.rep-port-stats {
  /* Push the stats to the bottom so the big title sits up top. */
  margin-top: auto;
  display: grid;
  grid-template-columns: 1fr auto;
  gap: 4px 12px;
  align-items: baseline;
}

.rep-port-stats dt {
  font-size: 12px;
  color: var(--text-secondary);
}

.rep-port-stats dd {
  margin: 0;
  text-align: right;
  font-size: 12.5px;
}

.v-err {
  color: var(--danger-color);
}

/* Center column: arrows around the toggle button */
.rep-center {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
}

.rep-flow {
  position: relative;
  width: 100%;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.rep-flow-tag {
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.08em;
  color: var(--text-muted);
  font-family: var(--font-mono);
  /* Sit above the connecting line so the value/arrowhead read cleanly. */
  position: relative;
  z-index: 1;
}

/* End-labels take the direction tone. */
.rep-flow-fwd .rep-flow-tag {
  color: var(--primary-color);
}

.rep-flow-rev .rep-flow-tag {
  color: var(--info);
}

/*
 * The arrow renders as a thin colored line that spans between the TX/RX
 * end-labels, with the byte value centered on top. A surface-colored gap
 * behind the value makes the line read as passing behind the number.
 */
.rep-arrow {
  position: absolute;
  left: 24px;
  right: 24px;
  top: 50%;
  transform: translateY(-50%);
  display: flex;
  align-items: center;
  justify-content: center;
}

/* The connecting line itself. */
.rep-arrow::before {
  content: '';
  position: absolute;
  left: 0;
  right: 0;
  top: 50%;
  height: 2px;
  transform: translateY(-50%);
  background: currentcolor;
}

/* Arrowhead (triangle) at the destination end. */
.rep-arrow::after {
  content: '';
  position: absolute;
  top: 50%;
  width: 0;
  height: 0;
  border-top: 5px solid transparent;
  border-bottom: 5px solid transparent;
}

.rep-arrow-right::after {
  right: -1px;
  transform: translateY(-50%);
  border-left: 8px solid currentcolor;
}

.rep-arrow-left::after {
  left: -1px;
  transform: translateY(-50%);
  border-right: 8px solid currentcolor;
}

/* Direction tone drives the line, arrowhead, and number color via currentcolor. */
.rep-flow-fwd .rep-arrow {
  color: var(--primary-color);
}

.rep-flow-rev .rep-arrow {
  color: var(--info);
}

.rep-arrow-value {
  position: relative;
  z-index: 1;
  font-size: 12px;
  font-weight: 600;
  color: currentcolor;
  white-space: nowrap;
  /* Interrupt the line behind the value. */
  background: var(--bg-surface);
  padding: 0 8px;
}

.rep-arrow-value em {
  font-style: normal;
  font-weight: 500;
  color: var(--text-muted);
}

/* The big enable/disable toggle button */
.rep-toggle {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  width: 100%;
  height: auto;
  padding: 16px 14px;
  border-radius: var(--r-lg);
  border: 1px solid var(--border-strong);
  background: var(--bg-surface);
  color: var(--text-secondary);
  cursor: pointer;
  transition: background 0.12s, border-color 0.12s, color 0.12s, box-shadow 0.12s;
  white-space: normal;
}

.rep-toggle:disabled {
  opacity: 0.6;
  cursor: not-allowed;
}

.rep-toggle.on {
  /* Soft green box for the enabled state. */
  background: var(--brand-soft);
  border-color: var(--brand-soft-border);
  color: var(--primary-color);
}

.rep-toggle.on:hover:not(:disabled) {
  /* Explicit background so the global button:hover green can't override it. */
  background: color-mix(in oklch, var(--primary-color) 14%, var(--bg-surface));
  border-color: var(--primary-color);
}

.rep-toggle.off:hover:not(:disabled) {
  /* Neutral light hover for the disabled state; explicit background defeats the
     global button:hover:not(:disabled) dark-green fill (no green tint here). */
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
  color: var(--text-color);
}

.rep-toggle-label {
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}

.rep-toggle-meta {
  display: flex;
  flex-direction: column;
  gap: 3px;
  width: 100%;
  margin: 2px 0;
}

.rep-toggle-meta > div {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  font-size: 11.5px;
}

.rep-toggle-meta span {
  color: var(--text-muted);
}

.rep-toggle-meta b {
  font-weight: 600;
  color: var(--text-secondary);
}

.rep-toggle-meta em {
  font-style: normal;
  color: var(--text-muted);
}

.rep-toggle-hint {
  font-size: 11px;
  color: var(--text-muted);
  font-weight: 400;
  letter-spacing: 0;
  text-transform: none;
}
</style>

<i18n>
{
  "en": {
    "title": "Repeater",
    "crumbs": "Transparent RS-485 passthrough between Port 1 and Port 2 to extend the line and restore signal integrity.",
    "warning_title": "Warning",
    "warning_body": "When the repeater is active, both RS-485 segments behave as if they were electrically connected. If masters are present on both sides, the buses will collide and communication will fail.",
    "bridge": "Bridge",
    "bridge_sub": "Port 1 ↔ Port 2",
    "port_1": "Port 1",
    "port_2": "Port 2",
    "enabled": "Enabled",
    "disabled": "Disabled",
    "click_to_disable": "Click to disable",
    "click_to_enable": "Click to enable",
    "uptime": "Uptime",
    "avg": "Avg",
    "dropped_bytes": "Dropped bytes"
  },
  "ru": {
    "title": "Повторитель",
    "crumbs": "Прозрачная передача RS-485 между Портом 1 и Портом 2 для удлинения линии и восстановления целостности сигнала.",
    "warning_title": "Внимание",
    "warning_body": "Когда повторитель активен, оба сегмента RS-485 ведут себя так, как будто они электрически соединены. Если мастеры присутствуют с обеих сторон, шины столкнутся и связь нарушится.",
    "bridge": "Мост",
    "bridge_sub": "Порт 1 ↔ Порт 2",
    "port_1": "Порт 1",
    "port_2": "Порт 2",
    "enabled": "Включён",
    "disabled": "Отключён",
    "click_to_disable": "Нажмите, чтобы отключить",
    "click_to_enable": "Нажмите, чтобы включить",
    "uptime": "Время работы",
    "avg": "Средн.",
    "dropped_bytes": "Потеряно байт"
  },
  "kk": {
    "title": "Қайталағыш",
    "crumbs": "Желіні ұзарту және сигнал тұтастығын қалпына келтіру үшін Порт 1 мен Порт 2 арасындағы мөлдір RS-485 беру.",
    "warning_title": "Назар аударыңыз",
    "warning_body": "Қайталағыш белсенді болғанда, екі RS-485 сегменті электрлік қосылғандай әрекет етеді. Егер мастерлер екі жақта да болса, шиналар соқтығысып, байланыс үзіледі.",
    "bridge": "Көпір",
    "bridge_sub": "Порт 1 ↔ Порт 2",
    "port_1": "Порт 1",
    "port_2": "Порт 2",
    "enabled": "Қосулы",
    "disabled": "Өшірілген",
    "click_to_disable": "Өшіру үшін басыңыз",
    "click_to_enable": "Қосу үшін басыңыз",
    "uptime": "Жұмыс уақыты",
    "avg": "Орташа",
    "dropped_bytes": "Жоғалған байт"
  },
  "it": {
    "title": "Ripetitore",
    "crumbs": "Passaggio RS-485 trasparente tra la Porta 1 e la Porta 2 per estendere la linea e ripristinare l'integrità del segnale.",
    "warning_title": "Attenzione",
    "warning_body": "Quando il ripetitore è attivo, entrambi i segmenti RS-485 si comportano come se fossero collegati elettricamente. Se sono presenti master su entrambi i lati, i bus entreranno in collisione e la comunicazione fallirà.",
    "bridge": "Bridge",
    "bridge_sub": "Porta 1 ↔ Porta 2",
    "port_1": "Porta 1",
    "port_2": "Porta 2",
    "enabled": "Abilitato",
    "disabled": "Disabilitato",
    "click_to_disable": "Fai clic per disabilitare",
    "click_to_enable": "Fai clic per abilitare",
    "uptime": "Tempo attivo",
    "avg": "Media",
    "dropped_bytes": "Byte persi"
  },
  "de": {
    "title": "Repeater",
    "crumbs": "Transparente RS-485-Durchleitung zwischen Port 1 und Port 2 zur Verlängerung der Leitung und Wiederherstellung der Signalintegrität.",
    "warning_title": "Warnung",
    "warning_body": "Wenn der Repeater aktiv ist, verhalten sich beide RS-485-Segmente, als wären sie elektrisch verbunden. Wenn auf beiden Seiten Master vorhanden sind, kollidieren die Busse und die Kommunikation schlägt fehl.",
    "bridge": "Brücke",
    "bridge_sub": "Port 1 ↔ Port 2",
    "port_1": "Port 1",
    "port_2": "Port 2",
    "enabled": "Aktiviert",
    "disabled": "Deaktiviert",
    "click_to_disable": "Zum Deaktivieren klicken",
    "click_to_enable": "Zum Aktivieren klicken",
    "uptime": "Laufzeit",
    "avg": "Durchschn.",
    "dropped_bytes": "Verworfene Bytes"
  }
}
</i18n>
