<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import {
  buildPreviewFrame,
  frameToPreviewParts,
  sendPacketToPort,
  parseModbusAddress,
  parseValueList,
} from '@/utils/modbusUtils';
import { senderState } from '@/utils/senderState';

const props = defineProps<{
  portNum: string;
  txDisabled: boolean;
}>();

const emit = defineEmits<{
  (e: 'close'): void;
}>();

const { t } = useI18n();

// Form fields live in a module-level singleton so they survive popup close/open (#1).
// Transient UI state (in-flight flag, error, success confirmation) stays component-local.
const state = senderState;
const sending = ref(false);
const error = ref('');
const success = ref('');

// FCs that write coils (boolean, value 0/1) as opposed to 16-bit registers.
const COIL_FCS = new Set(['05', '0f']);
// FCs that take a list of values (FC15/FC16) rather than a single value (FC05/FC06).
const LIST_FCS = new Set(['0f', '10']);

const readFcOptions = computed(() => [
  { value: '01', label: t('fc_opt_01') },
  { value: '02', label: t('fc_opt_02') },
  { value: '03', label: t('fc_opt_03') },
  { value: '04', label: t('fc_opt_04') },
]);

const writeFcOptions = computed(() => [
  { value: '05', label: t('fc_opt_05') },
  { value: '06', label: t('fc_opt_06') },
  { value: '0f', label: t('fc_opt_15') },
  { value: '10', label: t('fc_opt_16') },
]);

const fcOptions = computed(() => (state.mode === 'read' ? readFcOptions.value : writeFcOptions.value));

function setMode(m: 'read' | 'write') {
  state.mode = m;
  // Switching mode changes the FC, which triggers the fc watcher that resets `value`.
  state.fc = m === 'read' ? '03' : '06';
  error.value = '';
  success.value = '';
}

// Reset `value` to a sensible default whenever the FC changes so the form never carries an
// incompatible value into the new FC (e.g. "10" left over when switching to a coil FC that
// only accepts 0/1, which would silently disable the send button). Fires only on a real
// change — not on mount — so persisted values (#1) are preserved across reopen.
watch(() => state.fc, (fc) => {
  state.value = COIL_FCS.has(fc) ? '1' : '10';
  error.value = '';
  success.value = '';
});

const previewBytes = computed((): Uint8Array | null =>
  buildPreviewFrame(state.slaveId, state.fc, state.address, state.value, state.mode)
);

// Build a "preview" string: data bytes normal, last 2 bytes (CRC) in a wrapper span
// Returns an array of { hex, isCrc } objects
const previewParts = computed((): { hex: string; isCrc: boolean }[] => {
  const bytes = previewBytes.value;
  if (!bytes) return [];
  return frameToPreviewParts(bytes);
});

// When the preview is invalid in write mode, explain exactly why so the send button is never
// "dead" without a reason. Checks shared fields (slave, address) first, then the value per FC.
const writeHint = computed((): string => {
  if (state.mode !== 'write' || previewBytes.value !== null) return '';
  const slave = parseModbusAddress(state.slaveId);
  if (isNaN(slave) || slave < 1 || slave > 247) return t('hint_slave');
  const addr = parseModbusAddress(state.address);
  if (isNaN(addr) || addr < 0 || addr > 0xffff) return t('hint_addr');
  switch (state.fc) {
    case '06': return t('hint_reg');
    case '05': return t('hint_coil');
    case '10': {
      const vals = parseValueList(state.value);
      if (vals.length === 0) return t('hint_list');
      if (vals.length > 123) return t('hint_reg_count');
      return t('hint_reg');
    }
    case '0f': {
      const vals = parseValueList(state.value);
      if (vals.length === 0) return t('hint_list');
      if (vals.length > 1968) return t('hint_coil_count');
      return t('hint_coil');
    }
    default: return '';
  }
});

async function handleSend() {
  const bytes = previewBytes.value;
  if (!bytes) return;
  sending.value = true;
  error.value = '';
  success.value = '';
  try {
    // Convert bytes to compact hex string (no spaces)
    const hex = Array.from(bytes)
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
    const res = await sendPacketToPort(props.portNum, hex);
    // Keep the popup open and confirm the result so the user sees what was sent (#3c);
    // closing is only via the ✕ button.
    success.value = t('sent_ok', { n: res.sent, port: props.portNum });
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
  state.mode === 'read'
    ? t('send_read', { port: props.portNum })
    : t('send_write', { port: props.portNum })
);

// FC15/FC16 accept a list of values; FC05/FC06 a single value; read mode shows a count.
const isList = computed(() => state.mode === 'write' && LIST_FCS.has(state.fc));

const valueLabel = computed(() => {
  if (state.mode === 'read') return t('label_count');
  return isList.value ? t('label_values') : t('label_value');
});

const valuePlaceholder = computed(() => (isList.value ? t('placeholder_values') : ''));
</script>

<template>
  <div class="sniffer-sender">
    <div class="sniffer-sender-head">
      <span class="sniffer-sender-title">
        <span class="sniffer-sender-play">▶</span> {{ t('title') }}
      </span>
      <button class="sniffer-sender-close" :aria-label="t('close')" @click="emit('close')">✕</button>
    </div>

    <div class="sniffer-sender-seg">
      <button
        :class="['sniffer-sender-seg-btn', { 'sniffer-sender-seg-btnActive': state.mode === 'read' }]"
        @click="setMode('read')"
      >
{{ t('mode_read') }}
</button>
      <button
        :class="['sniffer-sender-seg-btn', { 'sniffer-sender-seg-btnActive': state.mode === 'write' }]"
        @click="setMode('write')"
      >
{{ t('mode_write') }}
</button>
    </div>

    <div class="sniffer-sender-body">
      <div class="form-grid">
        <!-- Slave ID -->
        <div class="form-field">
          <label class="form-field-label">{{ t('label_slave_id') }}</label>
          <input v-model="state.slaveId" class="form-field-input" />
        </div>

        <!-- Function code -->
        <div class="form-field">
          <label class="form-field-label">{{ t('label_fc') }}</label>
          <select v-model="state.fc" class="form-field-input">
            <option v-for="opt in fcOptions" :key="opt.value" :value="opt.value">{{ opt.label }}</option>
          </select>
        </div>

        <!-- Address -->
        <div class="form-field">
          <label class="form-field-label">{{ t('label_address') }}</label>
          <input v-model="state.address" class="form-field-input" />
        </div>

        <!-- Count / Value(s) -->
        <div class="form-field">
          <label class="form-field-label">{{ valueLabel }}</label>
          <input
            v-model="state.value"
            class="form-field-input"
            :inputmode="isList ? 'text' : 'numeric'"
            :placeholder="valuePlaceholder"
          />
        </div>
      </div>

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

      <!-- Explain why a write frame is invalid so the send button is never silently disabled -->
      <div v-if="writeHint" class="sniffer-sender-warn">{{ writeHint }}</div>
      <div v-if="success" class="sniffer-sender-success">{{ success }}</div>
      <div v-if="error" class="sniffer-sender-error">{{ error }}</div>
    </div>

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

.sniffer-sender-body {
  padding: 12px 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}

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

.sniffer-sender-error {
  color: var(--danger-color, #e53e3e);
  font-size: 12px;
  padding: 4px 0;
}

.sniffer-sender-warn {
  color: var(--text-secondary);
  font-size: 12px;
  padding: 4px 0;
}

.sniffer-sender-success {
  color: var(--mb-ok, #2f855a);
  font-size: 12px;
  padding: 4px 0;
}

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
    "label_values": "Values (comma-separated)",
    "placeholder_values": "e.g. 10, 20, 30",
    "preview_label": "PREVIEW",
    "hint_crc": "CRC computed automatically",
    "hint_tx_disabled": "TX is disabled for this port",
    "hint_slave": "Slave ID must be between 1 and 247",
    "hint_addr": "Address must be between 0 and 65535",
    "hint_reg": "Register value must be 0..65535",
    "hint_coil": "Coil value must be 0 or 1",
    "hint_list": "Enter values separated by commas",
    "hint_reg_count": "Up to 123 registers can be written at once",
    "hint_coil_count": "Up to 1968 coils can be written at once",
    "sent_ok": "Sent {n} bytes to port {port}",
    "send_read": "Send read to port {port}",
    "send_write": "Send write to port {port}",
    "fc_opt_01": "FC01 — Read Coils",
    "fc_opt_02": "FC02 — Read Discrete Inputs",
    "fc_opt_03": "FC03 — Read Holding Registers",
    "fc_opt_04": "FC04 — Read Input Registers",
    "fc_opt_05": "FC05 — Write Single Coil",
    "fc_opt_06": "FC06 — Write Single Register",
    "fc_opt_15": "FC15 — Write Multiple Coils",
    "fc_opt_16": "FC16 — Write Multiple Registers"
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
    "label_values": "Значения (через запятую)",
    "placeholder_values": "например 10, 20, 30",
    "preview_label": "PREVIEW",
    "hint_crc": "CRC добавляется автоматически",
    "hint_tx_disabled": "TX отключён для этого порта",
    "hint_slave": "Slave ID должен быть от 1 до 247",
    "hint_addr": "Адрес должен быть от 0 до 65535",
    "hint_reg": "Значение регистра должно быть 0..65535",
    "hint_coil": "Значение coil должно быть 0 или 1",
    "hint_list": "Введите значения через запятую",
    "hint_reg_count": "За один раз можно записать до 123 регистров",
    "hint_coil_count": "За один раз можно записать до 1968 coils",
    "sent_ok": "Отправлено {n} байт на порт {port}",
    "send_read": "Отправить чтение на порт {port}",
    "send_write": "Отправить запись на порт {port}",
    "fc_opt_01": "FC01 — Чтение Coils",
    "fc_opt_02": "FC02 — Чтение Discrete Inputs",
    "fc_opt_03": "FC03 — Чтение Holding Registers",
    "fc_opt_04": "FC04 — Чтение Input Registers",
    "fc_opt_05": "FC05 — Запись одного Coil",
    "fc_opt_06": "FC06 — Запись одного регистра",
    "fc_opt_15": "FC15 — Запись нескольких Coils",
    "fc_opt_16": "FC16 — Запись нескольких регистров"
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
    "label_values": "Мәндер (үтір арқылы)",
    "placeholder_values": "мысалы 10, 20, 30",
    "preview_label": "PREVIEW",
    "hint_crc": "CRC автоматты түрде есептеледі",
    "hint_tx_disabled": "Бұл порт үшін TX өшірілген",
    "hint_slave": "Slave ID 1 мен 247 аралығында болуы керек",
    "hint_addr": "Мекенжай 0 бен 65535 аралығында болуы керек",
    "hint_reg": "Регистр мәні 0..65535 болуы керек",
    "hint_coil": "Coil мәні 0 немесе 1 болуы керек",
    "hint_list": "Мәндерді үтір арқылы енгізіңіз",
    "hint_reg_count": "Бір уақытта 123 регистрге дейін жазуға болады",
    "hint_coil_count": "Бір уақытта 1968 coil-ге дейін жазуға болады",
    "sent_ok": "{port} портына {n} байт жіберілді",
    "send_read": "{port} портына оқуды жіберу",
    "send_write": "{port} портына жазуды жіберу",
    "fc_opt_01": "FC01 — Coils оқу",
    "fc_opt_02": "FC02 — Discrete Inputs оқу",
    "fc_opt_03": "FC03 — Holding Registers оқу",
    "fc_opt_04": "FC04 — Input Registers оқу",
    "fc_opt_05": "FC05 — Бір Coil жазу",
    "fc_opt_06": "FC06 — Бір регистр жазу",
    "fc_opt_15": "FC15 — Бірнеше Coils жазу",
    "fc_opt_16": "FC16 — Бірнеше регистр жазу"
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
    "label_values": "Valori (separati da virgola)",
    "placeholder_values": "es. 10, 20, 30",
    "preview_label": "ANTEPRIMA",
    "hint_crc": "CRC calcolato automaticamente",
    "hint_tx_disabled": "TX disabilitato per questa porta",
    "hint_slave": "Lo Slave ID deve essere compreso tra 1 e 247",
    "hint_addr": "L'indirizzo deve essere compreso tra 0 e 65535",
    "hint_reg": "Il valore del registro deve essere 0..65535",
    "hint_coil": "Il valore del coil deve essere 0 o 1",
    "hint_list": "Inserisci i valori separati da virgole",
    "hint_reg_count": "Si possono scrivere fino a 123 registri per volta",
    "hint_coil_count": "Si possono scrivere fino a 1968 coil per volta",
    "sent_ok": "Inviati {n} byte alla porta {port}",
    "send_read": "Invia lettura alla porta {port}",
    "send_write": "Invia scrittura alla porta {port}",
    "fc_opt_01": "FC01 — Lettura Coils",
    "fc_opt_02": "FC02 — Lettura Discrete Inputs",
    "fc_opt_03": "FC03 — Lettura Holding Registers",
    "fc_opt_04": "FC04 — Lettura Input Registers",
    "fc_opt_05": "FC05 — Scrittura singolo Coil",
    "fc_opt_06": "FC06 — Scrittura singolo registro",
    "fc_opt_15": "FC15 — Scrittura Coils multipli",
    "fc_opt_16": "FC16 — Scrittura registri multipli"
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
    "label_values": "Werte (kommagetrennt)",
    "placeholder_values": "z. B. 10, 20, 30",
    "preview_label": "VORSCHAU",
    "hint_crc": "CRC wird automatisch berechnet",
    "hint_tx_disabled": "TX für diesen Port deaktiviert",
    "hint_slave": "Slave-ID muss zwischen 1 und 247 liegen",
    "hint_addr": "Adresse muss zwischen 0 und 65535 liegen",
    "hint_reg": "Registerwert muss 0..65535 sein",
    "hint_coil": "Coil-Wert muss 0 oder 1 sein",
    "hint_list": "Werte durch Kommas getrennt eingeben",
    "hint_reg_count": "Es können bis zu 123 Register auf einmal geschrieben werden",
    "hint_coil_count": "Es können bis zu 1968 Coils auf einmal geschrieben werden",
    "sent_ok": "{n} Bytes an Port {port} gesendet",
    "send_read": "Lesen an Port {port} senden",
    "send_write": "Schreiben an Port {port} senden",
    "fc_opt_01": "FC01 — Coils lesen",
    "fc_opt_02": "FC02 — Discrete Inputs lesen",
    "fc_opt_03": "FC03 — Holding Registers lesen",
    "fc_opt_04": "FC04 — Input Registers lesen",
    "fc_opt_05": "FC05 — Einzelnes Coil schreiben",
    "fc_opt_06": "FC06 — Einzelnes Register schreiben",
    "fc_opt_15": "FC15 — Mehrere Coils schreiben",
    "fc_opt_16": "FC16 — Mehrere Register schreiben"
  }
}
</i18n>
