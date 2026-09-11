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
  // 'r' is FC03/FC04 only; 'rw' also takes FC16, which today is the 541–551 block alone.
  access: 'r' | 'rw';
  desc: { en: string; ru: string };
}

interface BiText {
  en: string;
  ru: string;
}

// Device own-address register map (Unit ID 255 / 0xFF), kept in sync with the README's table.
// The addresses, sizes, types and the notes match it one for one; a few descriptions are
// shortened here because this renders in a popup, so do not read "in sync" as "verbatim".
const intro: BiText = {
  en: 'The gateway answers Modbus polls on its own address Unit ID 255 (0xFF). It works in both Modbus TCP and Cache TCP modes, regardless of cache state. Read functions FC03 and FC04 are supported. Most of the map is read-only; the single exception is 541–551, which also takes FC16 — that is where the Z-Wave board writes its own state.',
  ru: 'Сам шлюз отвечает на Modbus-опрос по своему адресу Unit ID 255 (0xFF). Работает в режимах Modbus TCP и Cache TCP, независимо от состояния кэша. Поддерживаются функции чтения FC03 и FC04. Бо́льшая часть карты доступна только на чтение; единственное исключение — 541–551, этот блок принимает ещё и FC16: туда плата Z-Wave пишет своё состояние.',
};

// One table for both function codes: FC03 and FC04 answer the same value at the same address,
// so the rows are ordered purely by address. Where the Wiren Board common map files a field
// (input vs holding) is recorded in the first note instead of by a second table.
const regRows: RegRow[] = [
  { dec: '104–105', hex: '0x0068–0x0069', regs: 2, type: 'u32', access: 'r', desc: { en: 'Uptime since boot, seconds', ru: 'Время работы с момента загрузки, секунды' } },
  { dec: '121', hex: '0x0079', regs: 1, type: 'u16', access: 'r', desc: { en: 'Current supply voltage, mV', ru: 'Текущее напряжение питания, мВ' } },
  { dec: '200–219', hex: '0x00C8–0x00DB', regs: 20, type: 'string', access: 'r', desc: { en: 'Device model', ru: 'Модель устройства' } },
  { dec: '220–244', hex: '0x00DC–0x00F4', regs: 25, type: 'string', access: 'r', desc: { en: 'Commit hash and branch the firmware was built from', ru: 'Хэш коммита и ветка, откуда собрана прошивка' } },
  { dec: '250–265', hex: '0x00FA–0x0109', regs: 16, type: 'string', access: 'r', desc: { en: 'Firmware version (string)', ru: 'Версия прошивки (строкой)' } },
  { dec: '266–267', hex: '0x010A–0x010B', regs: 2, type: 'u32', access: 'r', desc: { en: 'Serial number generation scheme (4 = from MAC)', ru: 'Схема генерации серийного номера (4 — из MAC)' } },
  { dec: '268–271', hex: '0x010C–0x010F', regs: 4, type: 'u64', access: 'r', desc: { en: 'Serial number (MSW-first, 48-bit MAC)', ru: 'Серийный номер (MSW-first, 48-битный MAC)' } },
  { dec: '290–301', hex: '0x0122–0x012D', regs: 12, type: 'string', access: 'r', desc: { en: 'Firmware signature', ru: 'Сигнатура прошивки' } },
  { dec: '320', hex: '0x0140', regs: 1, type: 'u16', access: 'r', desc: { en: 'Firmware version: MAJOR', ru: 'Версия прошивки: MAJOR' } },
  { dec: '321', hex: '0x0141', regs: 1, type: 'u16', access: 'r', desc: { en: 'Firmware version: MINOR', ru: 'Версия прошивки: MINOR' } },
  { dec: '322', hex: '0x0142', regs: 1, type: 'u16', access: 'r', desc: { en: 'Firmware version: PATCH', ru: 'Версия прошивки: PATCH' } },
  { dec: '323', hex: '0x0143', regs: 1, type: 's16', access: 'r', desc: { en: 'Firmware version: SUFFIX (+N for +wbN, −N for -rcN, 0 if none)', ru: 'Версия прошивки: SUFFIX (+N для +wbN, −N для -rcN, 0 если нет)' } },
  { dec: '324–325', hex: '0x0144–0x0145', regs: 2, type: 'u32', access: 'r', desc: { en: 'Numeric firmware version (little-endian word order: 324 = low word)', ru: 'Версия в числовом формате (порядок слов little-endian: 324 — младшее слово)' } },
  { dec: '326–327', hex: '0x0146–0x0147', regs: 2, type: 'u32', access: 'r', desc: { en: 'Numeric firmware version (big-endian word order: 326 = high word)', ru: 'Версия в числовом формате (порядок слов big-endian: 326 — старшее слово)' } },
  { dec: '528–529', hex: '0x0210–0x0211', regs: 2, type: 'u32', access: 'r', desc: { en: 'Packets processed (since last cache reset)', ru: 'Количество обработанных пакетов (с последнего сброса кэша)' } },
  { dec: '530–531', hex: '0x0212–0x0213', regs: 2, type: 'u32', access: 'r', desc: { en: 'Seconds since the last packet on the bus', ru: 'Секунд с момента последнего пакета на шине' } },
  { dec: '532', hex: '0x0214', regs: 1, type: 'u16', access: 'r', desc: { en: 'Devices currently on the bus (unique slave_ids in cache)', ru: 'Количество устройств на шине (уникальных slave_id в кэше)' } },
  { dec: '533', hex: '0x0215', regs: 1, type: 'u16', access: 'r', desc: { en: 'Average bus poll rate, polls/min', ru: 'Средняя частота опроса шины, опросов/мин' } },
  { dec: '534', hex: '0x0216', regs: 1, type: 'u16', access: 'r', desc: { en: 'Cache value timeout, seconds', ru: 'Таймаут значения кэша, секунды' } },
  { dec: '535', hex: '0x0217', regs: 1, type: 'u16', access: 'r', desc: { en: 'Z-Wave inclusion request counter, non-volatile, increment-only', ru: 'Счётчик запросов включения в сеть Z-Wave, энергонезависимый, только растёт' } },
  { dec: '536', hex: '0x0218', regs: 1, type: 'u16', access: 'r', desc: { en: 'Airzone settings change counter, non-volatile, increment-only', ru: 'Счётчик изменений настроек Airzone, энергонезависимый, только растёт' } },
  { dec: '537', hex: '0x0219', regs: 1, type: 'u16', access: 'r', desc: { en: 'Airzone Modbus address, 1…247', ru: 'Modbus-адрес Airzone, 1…247' } },
  { dec: '538', hex: '0x021A', regs: 1, type: 'u16', access: 'r', desc: { en: 'Airzone zone, 1…32', ru: 'Зона Airzone, 1…32' } },
  { dec: '539', hex: '0x021B', regs: 1, type: 'u16', access: 'r', desc: { en: 'Airzone line speed, in units of 1200 baud, 4…96', ru: 'Скорость линии Airzone, в единицах по 1200 бод, 4…96' } },
  { dec: '540', hex: '0x021C', regs: 1, type: 'u16', access: 'r', desc: { en: 'Airzone product type, 0…3', ru: 'Тип устройства Airzone, 0…3' } },
  { dec: '541–551', hex: '0x021D–0x0227', regs: 11, type: 'u16', access: 'rw', desc: { en: 'State the Z-Wave board reports about itself, written by it with FC16', ru: 'Состояние, которое плата Z-Wave сообщает о себе; пишется ею через FC16' } },
  { dec: '65505', hex: '0xFFE1', regs: 1, type: 'u16', access: 'r', desc: { en: 'Total RAM, KB', ru: 'Полный объём оперативной памяти, КБ' } },
  { dec: '65506', hex: '0xFFE2', regs: 1, type: 'u16', access: 'r', desc: { en: 'Used RAM, KB', ru: 'Объём используемой оперативной памяти, КБ' } },
  { dec: '65507', hex: '0xFFE3', regs: 1, type: 'u16', access: 'r', desc: { en: 'Free RAM, KB', ru: 'Объём свободной оперативной памяти, КБ' } },
  { dec: '65508', hex: '0xFFE4', regs: 1, type: 'u16', access: 'r', desc: { en: 'Last MCU reboot reason', ru: 'Причина последней перезагрузки МК' } },
];

const notes: BiText[] = [
  { en: 'FC03 and FC04 share one address space: every address in the table answers on both function codes with the same value, which is why one table covers both. For cross-referencing the Wiren Board common register map: that map files the firmware signature (290–301) as holding registers and every other field listed here as an input register.', ru: 'FC03 и FC04 используют общее адресное пространство: каждый адрес таблицы отвечает по обеим функциям одним и тем же значением, поэтому таблица одна. Для сверки с общей картой регистров Wiren Board: там сигнатура прошивки (290–301) отнесена к holding-регистрам, а все остальные перечисленные здесь поля — к input-регистрам.' },
  { en: 'Strings (model, firmware version, signature): MEM_8 packing — 1 character per register in the low byte, high byte = 0x00; the tail is zero-padded.', ru: 'Строки (модель, версия прошивки, сигнатура): упаковка MEM_8 — 1 символ на регистр в младшем байте, старший байт = 0x00; хвост дополняется нулями.' },
  { en: 'Git info: 2 characters per register, first character in the low byte; the tail is zero-padded.', ru: 'Git-инфо: 2 символа на регистр, первый символ в младшем байте; хвост дополняется нулями.' },
  { en: 'Multi-register integers (except 324–325) use big-endian word order — the most significant word is at the lower register address.', ru: 'Многорегистровые целые (кроме 324–325) хранятся в порядке слов big-endian — старшее слово в младшем адресе.' },
  { en: 'Numeric version is computed per the Wiren Board rule: if (SUFFIX >= 0) enc = SUFFIX + 128; else enc = -1 - SUFFIX; VERSION = (MAJOR << 24) | (MINOR << 16) | (PATCH << 8) | enc.', ru: 'Числовая версия считается по правилу Wiren Board: if (SUFFIX >= 0) enc = SUFFIX + 128; else enc = -1 - SUFFIX; VERSION = (MAJOR << 24) | (MINOR << 16) | (PATCH << 8) | enc.' },
  { en: 'RAM block (65505–65507) follows the Wiren Board common register map (total / used / free), but in kilobytes rather than the bytes that map specifies: an ESP32 heap does not fit a u16 byte count. “Total RAM” is the total size of the internal heap, not the full SRAM of the chip, so it reads well below the datasheet figure. Of the WB diagnostics block (65504–65508), register 65504 — the “maximum used stack” slot — is the only one not implemented: this firmware is multi-tasking and has no single stack.', ru: 'Блок памяти (65505–65507) соответствует общей карте регистров Wiren Board (полный объём / используемый / свободный), но отдаётся в килобайтах, а не в байтах, как в той карте: куча ESP32 не помещается в u16 в байтах. «Полный объём» — это полный размер внутренней кучи, а не вся SRAM чипа, поэтому значение заметно меньше цифры из даташита. Из блока диагностики WB (65504–65508) не реализован только регистр 65504 — слот «максимальный использованный стек»: прошивка многозадачная, единого стека у неё нет.' },
  { en: 'Reboot reason (65508): 1 — LPWR (brownout / wake from sleep), 2 — WWDG (interrupt watchdog), 3 — IWDG (task / generic watchdog), 4 — SFT (software reset / panic), 5 — POR (power-on), 6 — PIN (external reset), 0 — unknown. Mapped from esp_reset_reason().', ru: 'Причина перезагрузки (65508): 1 — LPWR (brownout/выход из сна), 2 — WWDG (interrupt watchdog), 3 — IWDG (task/общий watchdog), 4 — SFT (программный сброс/паника), 5 — POR (включение питания), 6 — PIN (внешний сброс), 0 — неизвестно. Маппинг с esp_reset_reason().' },
  { en: 'Bus statistics (528–534) come from the multimaster cache; with the cache inactive these fields read as 0. The block sits at 528 to stay clear of the Wiren Board common register map, in particular of the bootloader-version field: 8 holding registers from 330 (330–337), left undefined here.', ru: 'Статистика шины (528–534) берётся из мультимастер-кэша; при неактивном кэше соответствующие поля читаются как 0. Блок вынесен на 528, чтобы не пересекаться с общей картой регистров Wiren Board — в первую очередь с полем версии загрузчика: 8 holding-регистров с адреса 330 (330–337), здесь они не определены.' },
  { en: 'Airzone settings (535–540) are what was entered here, in this firmware, for the Z-Wave board to read and apply — not what the board ended up running on. The board reads all six in one FC03, which is why they are contiguous; a range that reaches an address this map does not define is refused whole, so keep a read inside the block.', ru: 'Настройки Airzone (535–540) — это то, что задано здесь, в этой прошивке, чтобы плата Z-Wave прочитала и применила; это не то, на чём плата в итоге работает. Плата читает все шесть одним FC03, поэтому они идут подряд; чтение, диапазон которого захватывает адрес, не определённый в этой карте, отклоняется целиком — не выходите за пределы блока.' },
  { en: 'The board state block (541–551) is the only writable region of this map: it accepts FC16 (write multiple registers) for the whole block, and the Z-Wave board writes it itself roughly every 10 seconds. Its first four registers are what the board is actually running on, which is not necessarily what 537–540 asked for — the board validates each of the four values separately and refuses them one at a time.', ru: 'Блок состояния платы (541–551) — единственная доступная на запись область этой карты: он принимает FC16 (запись нескольких регистров) целиком, и плата Z-Wave пишет его сама примерно раз в 10 секунд. Первые четыре его регистра — то, на чём плата реально работает, а это не обязательно то, что задано в 537–540: плата проверяет каждое из четырёх значений отдельно и отклоняет их по одному.' },
  { en: 'Reading a range where at least one address is undefined returns exception 0x02 (illegal data address); a function other than FC03/FC04/FC16 returns exception 0x01 (illegal function). FC16 is accepted as a function wherever it is sent, but a write whose range is not entirely inside 541–551 returns 0x02 and stores nothing.', ru: 'Чтение диапазона, где хотя бы один адрес не определён, возвращает исключение 0x02 (illegal data address); функция, отличная от FC03/FC04/FC16, — исключение 0x01 (illegal function). Сама функция FC16 принимается на любом адресе, но запись, диапазон которой выходит за 541–551, возвращает 0x02 и ничего не сохраняет.' },
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

          <!-- Every register, ordered by address; both FC03 and FC04 read all of them -->
          <div class="drm-section-title">{{ t('section_regs') }}</div>
          <table class="drm-table">
            <thead>
              <tr>
                <th class="drm-col-mono">{{ t('col_dec') }}</th>
                <th class="drm-col-mono">{{ t('col_hex') }}</th>
                <th class="drm-col-mono">{{ t('col_regs') }}</th>
                <th class="drm-col-mono">{{ t('col_type') }}</th>
                <th>{{ t('col_access') }}</th>
                <th>{{ t('col_desc') }}</th>
              </tr>
            </thead>
            <tbody>
              <tr v-for="row in regRows" :key="row.dec">
                <td class="drm-mono drm-nowrap">{{ row.dec }}</td>
                <td class="drm-mono drm-nowrap">{{ row.hex }}</td>
                <td class="drm-mono">{{ row.regs }}</td>
                <td class="drm-mono drm-nowrap">{{ row.type }}</td>
                <td class="drm-nowrap">{{ row.access === 'rw' ? t('access_rw') : t('access_r') }}</td>
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
    "section_regs": "Registers (FC03/FC04; FC16 on 541–551)",
    "section_notes": "Notes",
    "col_dec": "Address (dec)",
    "col_hex": "Address (hex)",
    "col_regs": "Regs",
    "col_type": "Type",
    "col_access": "Access",
    "access_r": "read",
    "access_rw": "read/write",
    "col_desc": "Description",
    "close": "Close"
  },
  "ru": {
    "title": "Карта регистров устройства",
    "section_regs": "Регистры (FC03/FC04; FC16 на 541–551)",
    "section_notes": "Примечания",
    "col_dec": "Адрес (dec)",
    "col_hex": "Адрес (hex)",
    "col_regs": "Регистров",
    "col_type": "Тип",
    "col_access": "Доступ",
    "access_r": "чтение",
    "access_rw": "чтение/запись",
    "col_desc": "Описание",
    "close": "Закрыть"
  },
  "kk": {
    "title": "Құрылғы тіркеу картасы",
    "section_regs": "Регистрлер (FC03/FC04; 541–551 үшін FC16)",
    "section_notes": "Ескертпелер",
    "col_dec": "Мекенжай (dec)",
    "col_hex": "Мекенжай (hex)",
    "col_regs": "Регистр",
    "col_type": "Түрі",
    "col_access": "Қатынау",
    "access_r": "оқу",
    "access_rw": "оқу/жазу",
    "col_desc": "Сипаттама",
    "close": "Жабу"
  },
  "it": {
    "title": "Mappa registri del dispositivo",
    "section_regs": "Registri (FC03/FC04; FC16 su 541–551)",
    "section_notes": "Note",
    "col_dec": "Indirizzo (dec)",
    "col_hex": "Indirizzo (hex)",
    "col_regs": "Reg.",
    "col_type": "Tipo",
    "col_access": "Accesso",
    "access_r": "lettura",
    "access_rw": "lettura/scrittura",
    "col_desc": "Descrizione",
    "close": "Chiudi"
  },
  "de": {
    "title": "Geräte-Registerkarte",
    "section_regs": "Register (FC03/FC04; FC16 auf 541–551)",
    "section_notes": "Hinweise",
    "col_dec": "Adresse (dec)",
    "col_hex": "Adresse (hex)",
    "col_regs": "Reg.",
    "col_type": "Typ",
    "col_access": "Zugriff",
    "access_r": "lesen",
    "access_rw": "lesen/schreiben",
    "col_desc": "Beschreibung",
    "close": "Schließen"
  }
}
</i18n>
