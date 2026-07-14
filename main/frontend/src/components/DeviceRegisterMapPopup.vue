<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';

const emit = defineEmits<{
  (e: 'close'): void;
}>();

const { t, locale } = useI18n();

// Technical descriptions/notes/intro are taken verbatim from the EN and RU READMEs only.
// Those are the only two maintained docs, so kk/it/de deliberately fall back to the EN text
// (UI chrome strings are still fully localised via the <i18n> block below).
const lang = computed<'en' | 'ru'>(() => (locale.value === 'ru' ? 'ru' : 'en'));

const dialog = ref<HTMLDialogElement>();

// onMounted opens the native modal; the parent controls mount via v-if, so emit('close')
// (on Esc/backdrop/✕) is what tears the dialog down — we never leave it dangling open.
onMounted(() => {
  dialog.value?.showModal();
});

interface RegRow {
  dec: string;
  hex: string;
  regs: number;
  type: string;
  desc: { en: string; ru: string };
}

interface BiText {
  en: string;
  ru: string;
}

// Device own-address register map (Unit ID 255 / 0xFF), verbatim from the README.
const intro: BiText = {
  en: 'The gateway answers Modbus polls on its own address Unit ID 255 (0xFF). It works in both Modbus TCP and Cache TCP modes, regardless of cache state. Read functions FC04 (input) and FC03 (holding) are supported.',
  ru: 'Сам шлюз отвечает на Modbus-опрос по своему адресу Unit ID 255 (0xFF). Работает в режимах Modbus TCP и Cache TCP, независимо от состояния кэша. Поддерживаются функции чтения FC04 (input) и FC03 (holding).',
};

const inputRows: RegRow[] = [
  { dec: '104–105', hex: '0x0068–0x0069', regs: 2, type: 'u32', desc: { en: 'Uptime since boot, seconds', ru: 'Время работы с момента загрузки, секунды' } },
  { dec: '121', hex: '0x0079', regs: 1, type: 'u16', desc: { en: 'Current supply voltage, mV', ru: 'Текущее напряжение питания, мВ' } },
  { dec: '200–219', hex: '0x00C8–0x00DB', regs: 20, type: 'string', desc: { en: 'Device model', ru: 'Модель устройства' } },
  { dec: '220–244', hex: '0x00DC–0x00F4', regs: 25, type: 'string', desc: { en: 'Commit hash and branch the firmware was built from', ru: 'Хэш коммита и ветка, откуда собрана прошивка' } },
  { dec: '250–265', hex: '0x00FA–0x0109', regs: 16, type: 'string', desc: { en: 'Firmware version (string)', ru: 'Версия прошивки (строкой)' } },
  { dec: '266–269', hex: '0x010A–0x010D', regs: 4, type: 'u64', desc: { en: 'Serial number extension', ru: 'Расширение серийного номера' } },
  { dec: '270–271', hex: '0x010E–0x010F', regs: 2, type: 'u32', desc: { en: 'Serial number', ru: 'Серийный номер' } },
  { dec: '320', hex: '0x0140', regs: 1, type: 'u16', desc: { en: 'Firmware version: MAJOR', ru: 'Версия прошивки: MAJOR' } },
  { dec: '321', hex: '0x0141', regs: 1, type: 'u16', desc: { en: 'Firmware version: MINOR', ru: 'Версия прошивки: MINOR' } },
  { dec: '322', hex: '0x0142', regs: 1, type: 'u16', desc: { en: 'Firmware version: PATCH', ru: 'Версия прошивки: PATCH' } },
  { dec: '323', hex: '0x0143', regs: 1, type: 's16', desc: { en: 'Firmware version: SUFFIX (+N for +wbN, −N for -rcN, 0 if none)', ru: 'Версия прошивки: SUFFIX (+N для +wbN, −N для -rcN, 0 если нет)' } },
  { dec: '324–325', hex: '0x0144–0x0145', regs: 2, type: 'u32', desc: { en: 'Numeric firmware version (little-endian word order: 324 = low word)', ru: 'Версия в числовом формате (порядок слов little-endian: 324 — младшее слово)' } },
  { dec: '326–327', hex: '0x0146–0x0147', regs: 2, type: 'u32', desc: { en: 'Numeric firmware version (big-endian word order: 326 = high word)', ru: 'Версия в числовом формате (порядок слов big-endian: 326 — старшее слово)' } },
  { dec: '528–529', hex: '0x0210–0x0211', regs: 2, type: 'u32', desc: { en: 'Packets processed (since last cache reset)', ru: 'Количество обработанных пакетов (с последнего сброса кэша)' } },
  { dec: '530–531', hex: '0x0212–0x0213', regs: 2, type: 'u32', desc: { en: 'Seconds since the last packet on the bus', ru: 'Секунд с момента последнего пакета на шине' } },
  { dec: '532', hex: '0x0214', regs: 1, type: 'u16', desc: { en: 'Devices currently on the bus (unique slave_ids in cache)', ru: 'Количество устройств на шине (уникальных slave_id в кэше)' } },
  { dec: '533', hex: '0x0215', regs: 1, type: 'u16', desc: { en: 'Average bus poll rate, polls/min', ru: 'Средняя частота опроса шины, опросов/мин' } },
  { dec: '534', hex: '0x0216', regs: 1, type: 'u16', desc: { en: 'Cache value timeout, seconds', ru: 'Таймаут значения кэша, секунды' } },
  { dec: '65504', hex: '0xFFE0', regs: 1, type: 'u16', desc: { en: 'Maximum used stack, KB (0 = stack corrupted / unknown)', ru: 'Максимальный использованный стек, КБ (0 — стек повреждён / неизвестно)' } },
  { dec: '65505', hex: '0xFFE1', regs: 1, type: 'u16', desc: { en: 'Free RAM, KB', ru: 'Объём свободной оперативной памяти, КБ' } },
  { dec: '65506', hex: '0xFFE2', regs: 1, type: 'u16', desc: { en: 'Used RAM, KB', ru: 'Объём используемой оперативной памяти, КБ' } },
  { dec: '65507', hex: '0xFFE3', regs: 1, type: 'u16', desc: { en: 'Stack size, KB', ru: 'Размер стека, КБ' } },
  { dec: '65508', hex: '0xFFE4', regs: 1, type: 'u16', desc: { en: 'Last MCU reboot reason', ru: 'Причина последней перезагрузки МК' } },
];

const holdingRows: RegRow[] = [
  { dec: '290–301', hex: '0x0122–0x012D', regs: 12, type: 'string', desc: { en: 'Firmware signature', ru: 'Сигнатура прошивки' } },
];

const notes: BiText[] = [
  { en: 'FC03 and FC04 share one address space: every address in both tables answers on both function codes with the same value. The split only records where the Wiren Board common register map files each field.', ru: 'FC03 и FC04 используют общее адресное пространство: каждый адрес из обеих таблиц отвечает по обеим функциям одним и тем же значением. Разделение лишь показывает, куда поле отнесено в общей карте регистров Wiren Board.' },
  { en: 'Strings: 2 characters per register, high byte = first character; the tail is zero-padded.', ru: 'Строки: 2 символа на регистр, старший байт — первый символ; хвост дополняется нулями.' },
  { en: 'Multi-register integers (except 324–325) use big-endian word order — the most significant word is at the lower register address.', ru: 'Многорегистровые целые (кроме 324–325) хранятся в порядке слов big-endian — старшее слово в младшем адресе.' },
  { en: 'Numeric version is computed per the Wiren Board rule: if (SUFFIX >= 0) enc = SUFFIX + 128; else enc = -1 - SUFFIX; VERSION = (MAJOR << 24) | (MINOR << 16) | (PATCH << 8) | enc.', ru: 'Числовая версия считается по правилу Wiren Board: if (SUFFIX >= 0) enc = SUFFIX + 128; else enc = -1 - SUFFIX; VERSION = (MAJOR << 24) | (MINOR << 16) | (PATCH << 8) | enc.' },
  { en: 'Reboot reason (65508): 1 — LPWR (brownout / wake from sleep), 2 — WWDG (interrupt watchdog), 3 — IWDG (task / generic watchdog), 4 — SFT (software reset / panic), 5 — POR (power-on), 6 — PIN (external reset), 0 — unknown. Mapped from esp_reset_reason().', ru: 'Причина перезагрузки (65508): 1 — LPWR (brownout/выход из сна), 2 — WWDG (interrupt watchdog), 3 — IWDG (task/общий watchdog), 4 — SFT (программный сброс/паника), 5 — POR (включение питания), 6 — PIN (внешний сброс), 0 — неизвестно. Маппинг с esp_reset_reason().' },
  { en: 'Bus statistics (528–534) come from the multimaster cache; with the cache inactive these fields read as 0. The block sits at 528 to stay clear of the Wiren Board common register map, in particular of the bootloader-version field: 8 holding registers from 330 (330–337), left undefined here.', ru: 'Статистика шины (528–534) берётся из мультимастер-кэша; при неактивном кэше соответствующие поля читаются как 0. Блок вынесен на 528, чтобы не пересекаться с общей картой регистров Wiren Board — в первую очередь с полем версии загрузчика: 8 holding-регистров с адреса 330 (330–337), здесь они не определены.' },
  { en: 'Reading a range where at least one address is undefined returns exception 0x02 (illegal data address); a function other than FC03/FC04 returns exception 0x01 (illegal function).', ru: 'Чтение диапазона, где хотя бы один адрес не определён, возвращает исключение 0x02 (illegal data address); функция, отличная от FC03/FC04, — исключение 0x01 (illegal function).' },
];
</script>

<template>
  <Teleport to="body">
    <dialog
      ref="dialog"
      class="drm-dialog"
      @click.self="emit('close')"
      @cancel.prevent="emit('close')"
      @close="emit('close')"
    >
      <div class="drm-card">
        <!-- Header: title + fixed technical Unit ID sublabel, close button on the right -->
        <div class="drm-head">
          <div class="drm-head-titles">
            <span class="drm-title">{{ t('title') }}</span>
            <span class="drm-sublabel">Unit ID 255 (0xFF)</span>
          </div>
          <button class="drm-close" :aria-label="t('close')" @click="emit('close')">✕</button>
        </div>

        <div class="drm-body">
          <p class="drm-intro">{{ intro[lang] }}</p>

          <!-- Input registers (FC04) -->
          <div class="drm-section-title">{{ t('section_input') }}</div>
          <table class="drm-table">
            <thead>
              <tr>
                <th class="drm-col-mono">{{ t('col_dec') }}</th>
                <th class="drm-col-mono">{{ t('col_hex') }}</th>
                <th class="drm-col-mono">{{ t('col_regs') }}</th>
                <th class="drm-col-mono">{{ t('col_type') }}</th>
                <th>{{ t('col_desc') }}</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="row in inputRows" :key="row.dec">
                <td class="drm-mono drm-nowrap">{{ row.dec }}</td>
                <td class="drm-mono drm-nowrap">{{ row.hex }}</td>
                <td class="drm-mono">{{ row.regs }}</td>
                <td class="drm-mono drm-nowrap">{{ row.type }}</td>
                <td>{{ row.desc[lang] }}</td>
              </tr>
            </tbody>
          </table>

          <!-- Holding registers (FC03) -->
          <div class="drm-section-title">{{ t('section_holding') }}</div>
          <table class="drm-table">
            <thead>
              <tr>
                <th class="drm-col-mono">{{ t('col_dec') }}</th>
                <th class="drm-col-mono">{{ t('col_hex') }}</th>
                <th class="drm-col-mono">{{ t('col_regs') }}</th>
                <th class="drm-col-mono">{{ t('col_type') }}</th>
                <th>{{ t('col_desc') }}</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="row in holdingRows" :key="row.dec">
                <td class="drm-mono drm-nowrap">{{ row.dec }}</td>
                <td class="drm-mono drm-nowrap">{{ row.hex }}</td>
                <td class="drm-mono">{{ row.regs }}</td>
                <td class="drm-mono drm-nowrap">{{ row.type }}</td>
                <td>{{ row.desc[lang] }}</td>
              </tr>
            </tbody>
          </table>

          <div class="drm-section-title">{{ t('section_notes') }}</div>
          <ul class="drm-notes">
            <li v-for="(note, i) in notes" :key="i">{{ note[lang] }}</li>
          </ul>
        </div>
      </div>
    </dialog>
  </Teleport>
</template>

<style scoped>
.drm-dialog {
  padding: 0;
  border: none;
  background: transparent;
  max-width: 720px;
  width: calc(100vw - 48px);
  margin: auto;
  overflow: visible;
}

.drm-dialog::backdrop {
  background: rgba(7, 7, 7, 0.8);
}

.drm-card {
  display: flex;
  flex-direction: column;
  max-height: 85vh;
  background: var(--bg-surface);
  color: var(--text-color);
  border: 1px solid var(--border-color);
  border-radius: var(--r-lg);
  box-shadow: 0 0 20px 4px rgba(0, 0, 0, 0.4);
  font-family: var(--font-ui);
  overflow: hidden;
}

.drm-head {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 12px;
  padding: 16px 18px;
  border-bottom: 1px solid var(--border-color);
  flex-shrink: 0;
}

.drm-head-titles {
  display: flex;
  align-items: baseline;
  gap: 10px;
  flex-wrap: wrap;
}

.drm-title {
  font-size: 16px;
  font-weight: 600;
  color: var(--text-color);
}

.drm-sublabel {
  font-family: var(--font-mono);
  font-size: 12px;
  color: var(--text-muted);
}

.drm-close {
  background: transparent;
  border: none;
  cursor: pointer;
  color: var(--text-muted);
  font-size: 16px;
  line-height: 1;
  padding: 2px 4px;
  border-radius: var(--r-sm);
  flex-shrink: 0;
  transition: color 0.12s, background 0.12s;
}

.drm-close:hover {
  color: var(--text-color);
  background: var(--bg-surface-subtle);
}

.drm-body {
  padding: 16px 18px 20px;
  overflow: auto;
}

.drm-intro {
  margin: 0 0 16px;
  font-size: 13px;
  line-height: 1.5;
  color: var(--text-secondary);
}

.drm-section-title {
  margin: 18px 0 8px;
  font-size: 11px;
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: 0.07em;
  color: var(--text-muted);
}

.drm-section-title:first-of-type {
  margin-top: 0;
}

.drm-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 12.5px;
}

.drm-table th {
  text-align: left;
  padding: 6px 10px;
  border-bottom: 1px solid var(--border-strong);
  font-size: 11px;
  font-weight: 600;
  color: var(--text-muted);
  white-space: nowrap;
}

.drm-table td {
  padding: 6px 10px;
  border-bottom: 1px solid var(--border-color);
  color: var(--text-color);
  vertical-align: top;
}

.drm-table tbody tr:last-child td {
  border-bottom: none;
}

.drm-col-mono {
  font-family: var(--font-mono);
}

.drm-mono {
  font-family: var(--font-mono);
  color: var(--text-secondary);
}

.drm-nowrap {
  white-space: nowrap;
}

.drm-notes {
  margin: 0;
  padding-left: 18px;
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.drm-notes li {
  font-size: 12px;
  line-height: 1.5;
  color: var(--text-muted);
}
</style>

<i18n>
{
  "en": {
    "title": "Device register map",
    "section_input": "Input registers (FC04, read-only)",
    "section_holding": "Holding registers (FC03, read-only)",
    "section_notes": "Notes",
    "col_dec": "Address (dec)",
    "col_hex": "Address (hex)",
    "col_regs": "Regs",
    "col_type": "Type",
    "col_desc": "Description",
    "close": "Close"
  },
  "ru": {
    "title": "Карта регистров устройства",
    "section_input": "Input-регистры (FC04, только чтение)",
    "section_holding": "Holding-регистры (FC03, только чтение)",
    "section_notes": "Примечания",
    "col_dec": "Адрес (dec)",
    "col_hex": "Адрес (hex)",
    "col_regs": "Регистров",
    "col_type": "Тип",
    "col_desc": "Описание",
    "close": "Закрыть"
  },
  "kk": {
    "title": "Құрылғы тіркеу картасы",
    "section_input": "Input регистрлері (FC04, тек оқу)",
    "section_holding": "Holding регистрлері (FC03, тек оқу)",
    "section_notes": "Ескертпелер",
    "col_dec": "Мекенжай (dec)",
    "col_hex": "Мекенжай (hex)",
    "col_regs": "Регистр",
    "col_type": "Түрі",
    "col_desc": "Сипаттама",
    "close": "Жабу"
  },
  "it": {
    "title": "Mappa registri del dispositivo",
    "section_input": "Registri input (FC04, sola lettura)",
    "section_holding": "Registri holding (FC03, sola lettura)",
    "section_notes": "Note",
    "col_dec": "Indirizzo (dec)",
    "col_hex": "Indirizzo (hex)",
    "col_regs": "Reg.",
    "col_type": "Tipo",
    "col_desc": "Descrizione",
    "close": "Chiudi"
  },
  "de": {
    "title": "Geräte-Registerkarte",
    "section_input": "Input-Register (FC04, schreibgeschützt)",
    "section_holding": "Holding-Register (FC03, schreibgeschützt)",
    "section_notes": "Hinweise",
    "col_dec": "Adresse (dec)",
    "col_hex": "Adresse (hex)",
    "col_regs": "Reg.",
    "col_type": "Typ",
    "col_desc": "Beschreibung",
    "close": "Schließen"
  }
}
</i18n>
