<script setup lang="ts">
import { ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import {
  buildPreviewFrame,
  frameToPreviewParts,
  sendPacketToPort,
} from '@/utils/modbusUtils';

const props = defineProps<{
  portNum: string;
  txDisabled: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

const { t } = useI18n();

const mode = ref<'read' | 'write'>('read');
const slaveId = ref('01');
const fc = ref('03');
const address = ref('0x0000');
const value = ref('10');
const sending = ref(false);
const error = ref('');

// Read mode FC options
const readFcOptions = [
  { value: '01', label: 'FC01 — Read Coils' },
  { value: '02', label: 'FC02 — Read Discrete Inputs' },
  { value: '03', label: 'FC03 — Read Holding Registers' },
  { value: '04', label: 'FC04 — Read Input Registers' },
];

// Write mode FC options
const writeFcOptions = [
  { value: '05', label: 'FC05 — Write Single Coil' },
  { value: '06', label: 'FC06 — Write Single Register' },
  { value: '0f', label: 'FC15 — Write Multiple Coils' },
  { value: '10', label: 'FC16 — Write Multiple Registers' },
];

const fcOptions = computed(() => (mode.value === 'read' ? readFcOptions : writeFcOptions));

// Switch mode and reset fc to first option for that mode
function setMode(m: 'read' | 'write') {
  mode.value = m;
  fc.value = m === 'read' ? '03' : '06';
  error.value = '';
}

// Build the current RTU frame based on inputs, or return null on bad input
const previewBytes = computed((): Uint8Array | null =>
  buildPreviewFrame(slaveId.value, fc.value, address.value, value.value, mode.value)
);

// Build a "preview" string: data bytes normal, last 2 bytes (CRC) in a wrapper span
// Returns an array of { hex, isCrc } objects
const previewParts = computed((): { hex: string; isCrc: boolean }[] => {
  const bytes = previewBytes.value;
  if (!bytes) return [];
  return frameToPreviewParts(bytes);
});

async function handleSend() {
  const bytes = previewBytes.value;
  if (!bytes) return;
  sending.value = true;
  error.value = '';
  try {
    // Convert bytes to compact hex string (no spaces)
    const hex = Array.from(bytes)
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
    await sendPacketToPort(props.portNum, hex);
    emit('close');
  } catch (e: unknown) {
    error.value = e instanceof Error ? e.message : String(e);
  } finally {
    sending.value = false;
  }
}

const sendDisabled = computed(() =>
  props.txDisabled || sending.value || previewBytes.value === null
);

const sendLabel = computed(() =>
  mode.value === 'read'
    ? t('send_read', { port: props.portNum })
    : t('send_write', { port: props.portNum })
);

const valueLabel = computed(() =>
  mode.value === 'read' ? t('label_count') : t('label_value')
);
</script>

<template>
  <div class="sniffer-sender">
    <!-- Header -->
    <div class="sniffer-sender-head">
      <span class="sniffer-sender-title">
        <span class="sniffer-sender-play">▶</span> {{ t('title') }}
      </span>
      <button class="sniffer-sender-close" :aria-label="t('close')" @click="emit('close')">✕</button>
    </div>

    <!-- Read / Write segmented toggle -->
    <div class="sniffer-sender-seg">
      <button
        :class="['sniffer-sender-seg-btn', { 'sniffer-sender-seg-btnActive': mode === 'read' }]"
        @click="setMode('read')"
      >
{{ t('mode_read') }}
</button>
      <button
        :class="['sniffer-sender-seg-btn', { 'sniffer-sender-seg-btnActive': mode === 'write' }]"
        @click="setMode('write')"
      >
{{ t('mode_write') }}
</button>
    </div>

    <!-- Form body -->
    <div class="sniffer-sender-body">
      <div class="form-grid">
        <!-- Slave ID -->
        <div class="form-field">
          <label class="form-field-label">{{ t('label_slave_id') }}</label>
          <input v-model="slaveId" class="form-field-input" />
        </div>

        <!-- Function code -->
        <div class="form-field">
          <label class="form-field-label">{{ t('label_fc') }}</label>
          <select v-model="fc" class="form-field-input">
            <option v-for="opt in fcOptions" :key="opt.value" :value="opt.value">{{ opt.label }}</option>
          </select>
        </div>

        <!-- Address -->
        <div class="form-field">
          <label class="form-field-label">{{ t('label_address') }}</label>
          <input v-model="address" class="form-field-input" />
        </div>

        <!-- Count / Value -->
        <div class="form-field">
          <label class="form-field-label">{{ valueLabel }}</label>
          <input v-model="value" class="form-field-input" inputmode="numeric" />
        </div>
      </div>

      <!-- Preview bar -->
      <div class="sniffer-sender-preview">
        <span class="sniffer-sender-preview-label">{{ t('preview_label') }}</span>
        <span class="sniffer-sender-preview-bytes">
          <template v-if="previewParts.length > 0">
            <span
              v-for="(part, i) in previewParts"
              :key="i"
              :class="['sniffer-sender-preview-byte', { 'sniffer-sender-preview-byteCrc': part.isCrc }]"
            >{{ part.hex }}</span>
          </template>
          <span v-else class="sniffer-sender-preview-empty">—</span>
        </span>
      </div>

      <!-- Error message -->
      <div v-if="error" class="sniffer-sender-error">{{ error }}</div>
    </div>

    <!-- Footer -->
    <div class="sniffer-sender-foot">
      <button
        class="sniffer-sender-foot-send"
        :disabled="sendDisabled"
        @click="handleSend"
      >
▶ {{ sendLabel }}
</button>
      <!-- CRC hint or TX-disabled warning below the send button -->
      <span class="sniffer-sender-hint">
        <template v-if="txDisabled">{{ t('hint_tx_disabled') }}</template>
        <template v-else>{{ t('hint_crc') }}</template>
      </span>
    </div>
  </div>
</template>

<style scoped>
.sniffer-sender {
  position: absolute;
  top: 16px;
  right: 16px;
  z-index: 100;
  width: 480px;
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: var(--r-lg, 10px);
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.14), 0 2px 8px rgba(0, 0, 0, 0.08);
  display: flex;
  flex-direction: column;
  font-size: 13px;
}

/* Header */
.sniffer-sender-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 14px 16px 10px;
  border-bottom: 1px solid var(--border-color);
}

.sniffer-sender-title {
  font-weight: 600;
  font-size: 14px;
  color: var(--text-color);
  display: flex;
  align-items: center;
  gap: 6px;
}

.sniffer-sender-play {
  color: var(--primary-color);
  font-size: 12px;
}

.sniffer-sender-close {
  background: transparent;
  border: none;
  cursor: pointer;
  color: var(--text-muted);
  font-size: 16px;
  line-height: 1;
  padding: 2px 4px;
  border-radius: var(--r-sm, 4px);
  transition: color 0.12s, background 0.12s;
}

.sniffer-sender-close:hover {
  color: var(--text-color);
  background: var(--bg-surface-subtle);
}

/* Segmented control */
.sniffer-sender-seg {
  display: flex;
  margin: 12px 16px 0;
  border: 1px solid var(--border-color);
  border-radius: var(--r-sm, 4px);
  overflow: hidden;
}

.sniffer-sender-seg-btn {
  flex: 1;
  height: 32px;
  border: none;
  background: transparent;
  color: var(--text-secondary);
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.12s, color 0.12s;
}

.sniffer-sender-seg-btn:hover:not(.sniffer-sender-seg-btnActive) {
  background: var(--bg-surface-subtle);
}

.sniffer-sender-seg-btnActive {
  background: var(--primary-color);
  color: #fff;
}

/* Body */
.sniffer-sender-body {
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

/* 2-column form grid */
.form-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px 12px;
}

.form-field {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.form-field-label {
  font-size: 10.5px;
  text-transform: uppercase;
  letter-spacing: 0.07em;
  color: var(--text-muted);
  font-weight: 500;
}

.form-field-input {
  height: 32px;
  padding: 0 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--r-sm, 4px);
  background: var(--bg-surface);
  color: var(--text-color);
  font-size: 13px;
  font-family: var(--font-ui);
  transition: border-color 0.12s;
  outline: none;
}

.form-field-input:focus {
  border-color: var(--primary-color);
}

/* Preview bar */
.sniffer-sender-preview {
  background: var(--bg-surface-subtle);
  border: 1px solid var(--border-color);
  border-radius: var(--r-sm, 4px);
  padding: 8px 12px;
  display: flex;
  gap: 8px;
  align-items: center;
  min-height: 36px;
  flex-wrap: wrap;
}

.sniffer-sender-preview-label {
  font-size: 10px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: var(--text-muted);
  font-weight: 600;
  flex-shrink: 0;
}

.sniffer-sender-preview-bytes {
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
  font-family: var(--font-mono);
  font-size: 12px;
}

.sniffer-sender-preview-byte {
  color: var(--text-color);
}

.sniffer-sender-preview-byteCrc {
  color: var(--mb-hex-crc);
}

.sniffer-sender-preview-empty {
  color: var(--text-muted);
}

/* Error */
.sniffer-sender-error {
  color: var(--danger-color, #e53e3e);
  font-size: 12px;
  padding: 4px 0;
}

/* Footer */
.sniffer-sender-foot {
  display: flex;
  flex-direction: column;
  align-items: stretch;
  padding: 10px 16px 14px;
  border-top: 1px solid var(--border-color);
  gap: 6px;
}

.sniffer-sender-hint {
  font-size: 11px;
  color: var(--text-muted);
  font-style: italic;
  text-align: center;
}

.sniffer-sender-foot-send {
  width: 100%;
  height: 34px;
  padding: 0 14px;
  border: 1px solid var(--primary-color);
  border-radius: var(--r-md, 6px);
  background: var(--primary-color);
  color: #fff;
  font-size: 13px;
  font-weight: 500;
  cursor: pointer;
  transition: background 0.12s, border-color 0.12s, opacity 0.12s;
}

.sniffer-sender-foot-send:hover:not(:disabled) {
  background: var(--primary-color-hover);
  border-color: var(--primary-color-hover);
}

.sniffer-sender-foot-send:disabled {
  opacity: 0.55;
  cursor: not-allowed;
}
</style>

<i18n>
{
  "en": {
    "title": "Send packet",
    "close": "Close",
    "mode_read": "Read",
    "mode_write": "Write",
    "label_slave_id": "Slave ID",
    "label_fc": "Function code",
    "label_address": "Start address",
    "label_count": "Count",
    "label_value": "Value",
    "preview_label": "PREVIEW",
    "hint_crc": "CRC computed automatically",
    "hint_tx_disabled": "TX is disabled for this port",
    "send_read": "Send read to port {port}",
    "send_write": "Send write to port {port}"
  },
  "ru": {
    "title": "Отправить пакет",
    "close": "Закрыть",
    "mode_read": "Чтение",
    "mode_write": "Запись",
    "label_slave_id": "Slave ID",
    "label_fc": "Код функции",
    "label_address": "Адрес",
    "label_count": "Количество",
    "label_value": "Значение",
    "preview_label": "PREVIEW",
    "hint_crc": "CRC добавляется автоматически",
    "hint_tx_disabled": "TX отключён для этого порта",
    "send_read": "Отправить чтение на порт {port}",
    "send_write": "Отправить запись на порт {port}"
  },
  "kk": {
    "title": "Пакет жіберу",
    "close": "Жабу",
    "mode_read": "Оқу",
    "mode_write": "Жазу",
    "label_slave_id": "Slave ID",
    "label_fc": "Функция коды",
    "label_address": "Мекенжай",
    "label_count": "Саны",
    "label_value": "Мән",
    "preview_label": "PREVIEW",
    "hint_crc": "CRC автоматты түрде есептеледі",
    "hint_tx_disabled": "Бұл порт үшін TX өшірілген",
    "send_read": "{port} портына оқуды жіберу",
    "send_write": "{port} портына жазуды жіберу"
  },
  "it": {
    "title": "Invia pacchetto",
    "close": "Chiudi",
    "mode_read": "Lettura",
    "mode_write": "Scrittura",
    "label_slave_id": "Slave ID",
    "label_fc": "Codice funzione",
    "label_address": "Indirizzo",
    "label_count": "Conteggio",
    "label_value": "Valore",
    "preview_label": "ANTEPRIMA",
    "hint_crc": "CRC calcolato automaticamente",
    "hint_tx_disabled": "TX disabilitato per questa porta",
    "send_read": "Invia lettura alla porta {port}",
    "send_write": "Invia scrittura alla porta {port}"
  },
  "de": {
    "title": "Paket senden",
    "close": "Schließen",
    "mode_read": "Lesen",
    "mode_write": "Schreiben",
    "label_slave_id": "Slave-ID",
    "label_fc": "Funktionscode",
    "label_address": "Adresse",
    "label_count": "Anzahl",
    "label_value": "Wert",
    "preview_label": "VORSCHAU",
    "hint_crc": "CRC wird automatisch berechnet",
    "hint_tx_disabled": "TX für diesen Port deaktiviert",
    "send_read": "Lesen an Port {port} senden",
    "send_write": "Schreiben an Port {port} senden"
  }
}
</i18n>
