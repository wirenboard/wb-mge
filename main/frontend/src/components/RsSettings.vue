<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import type { Baudrate, Databits, Parity, RsSettings, Settings, Stopbits } from '@/common/types';
import Button from '@/components/Button.vue';
import Switch from '@/components/Switch.vue';

const props = defineProps<{ title: string; sub?: string; field: string; signature?: string }>();

const { t } = useI18n();
const { isChanged, isLoading, updateSettings } = useSettings();
const settings = defineModel<RsSettings>('settings');

const baudrateOptions: Baudrate[] = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

const parityOptions: Parity[] = ['none', 'even', 'odd'];

const stopBits: Stopbits[] = ['1', '1.5', '2'];

const dataBits: Databits[] = ['5', '6', '7', '8'];

const save = () => {
  const data: Partial<Settings> = {
    [props.field]: settings.value,
  };
  updateSettings(data);
};

// I/O Bus lives in its own card with a dedicated Save button (see SerialPorts.vue),
// so io_bus changes must not enable the RS-485 Port 2 Save. Track only this port's field.
const isSaveDisabled = computed(() => isLoading.value || !isChanged([props.field]));

// WB-MGE (mge_v3) shares the RS-485-2 bus with the on-board WB-MIO. Disabling TX on
// port 2 does NOT switch WB-MIO off — that is the separate I/O Bus control, which drives
// the MIO reset line — it keeps answering any other master on the segment, but the
// gateway can no longer forward TCP requests to it. Specific to that board/port.
const showMioWarning = computed(() =>
  props.field === 'rs485_2' && props.signature === 'mge_v3' && !!settings.value?.tx_disabled
);
</script>

<template>
  <section class="card">
    <form @submit.prevent="save">
      <div class="card-header">
        <div class="card-title-wrap">
          <div class="title">{{ title }}</div>
          <div v-if="sub" class="sub">{{ sub }}</div>
        </div>
        <Button
          type="submit"
          :is-loading="isLoading"
          :disabled="isSaveDisabled"
        >
          {{ t('save') }}
        </Button>
      </div>
      <div class="card-body">
        <div class="field">
          <label :for="`${field}-baudrate`">{{ t('baudrate') }}</label>
          <select :id="`${field}-baudrate`" v-model="settings!.baudrate" name="baudrate">
            <option v-for="item in baudrateOptions" :key="`baudrate_1_${item}`" :value="item">{{ item }}</option>
          </select>
        </div>
        <div class="field">
          <label :for="`${field}-parity`">{{ t('parity') }}</label>
          <select :id="`${field}-parity`" v-model="settings!.parity" name="parity">
            <option v-for="item in parityOptions" :key="`parity_1_${item}`" :value="item">{{ t(item) }}</option>
          </select>
        </div>
        <div class="field">
          <label :for="`${field}-stopbits`">{{ t('stopbits') }}</label>
          <select :id="`${field}-stopbits`" v-model="settings!.stopbits" name="stopbits">
            <option v-for="item in stopBits" :key="`stopbits_1_${item}`" :value="item">{{ item?.split('-')[0] }}</option>
          </select>
        </div>
        <div class="field">
          <label :for="`${field}-databits`">{{ t('databits') }}</label>
          <select :id="`${field}-databits`" v-model="settings!.databits" name="databits">
            <option v-for="item in dataBits" :key="`stopbits_1_${item}`" :value="item">{{ item?.split('-')[0] }}</option>
          </select>
        </div>
        <div class="field">
          <label :for="`${field}-fail_safe`">{{ t('failsafe') }}</label>
          <div class="field-switch"><Switch :id="`${field}-fail_safe`" v-model="settings!.fail_safe" /></div>
          <div class="field-hint">{{ t('failsafe_hint') }}</div>
        </div>
        <div class="field">
          <label :for="`${field}-term`">{{ t('terminator') }}</label>
          <div class="field-switch"><Switch :id="`${field}-term`" v-model="settings!.term" /></div>
          <div class="field-hint">{{ t('terminator_hint') }}</div>
        </div>
        <div class="field">
          <label :for="`${field}-tx_disabled`">{{ t('tx_disabled') }}</label>
          <div class="field-switch"><Switch :id="`${field}-tx_disabled`" v-model="settings!.tx_disabled" /></div>
          <div v-if="settings!.tx_disabled" class="field-warning">
            <span class="field-warning-icon">⚠</span>
            {{ t('tx_disabled_warning') }}
          </div>
          <div v-if="showMioWarning" class="field-warning">
            <span class="field-warning-icon">⚠</span>
            {{ t('tx_disabled_mio_warning') }}
          </div>
        </div>
      </div>
    </form>
  </section>
</template>

<style scoped>
.field-switch {
  justify-self: end;
}

.field-hint {
  grid-column: 1 / -1;
  font-size: 11px;
  color: var(--text-muted);
  margin-top: -10px;
  line-height: 1.4;
  /* Preserve intentional line breaks (\n) in hint texts; normal text still wraps */
  white-space: pre-line;
}

.field-warning {
  grid-column: 1 / -1;
  display: flex;
  gap: 6px;
  align-items: flex-start;
  font-size: 12px;
  color: var(--warn);
  margin-top: -10px;
  line-height: 1.4;
}

.field-warning-icon {
  flex-shrink: 0;
}
</style>

<i18n>
{
  "en": {
    "baudrate": "Baud rate",
    "parity": "Parity",
    "even": "Even",
    "odd": "Odd",
    "stopbits": "Stop bits",
    "databits": "Data bits",
    "failsafe": "Failsafe bias",
    "failsafe_hint": "560 Ω bias resistors that pull the A/B lines apart, removing the undefined bus state when device transmitters are idle. Enable when the module acts as bus master; otherwise leave it off.",
    "terminator": "120Ω termination resistor",
    "terminator_hint": "120 Ω resistor connected between lines A and B. \nEnable when the module is at the end of the bus; if it sits in the middle, leave it off.",
    "tx_disabled": "Disable transmission (TX)",
    "tx_disabled_warning": "Disabling TX will break bridge modes — only sniffer and cache bus will remain fully functional. Bridge connections will only forward data from RS-485 to TCP, but will not send data from TCP into the RS-485 bus.",
    "tx_disabled_mio_warning": "WB-MIO is not switched off: it stays on the shared RS-485-2 bus and keeps answering any other master. Only the gateway loses access to it — with TX disabled it cannot forward requests from TCP; use the I/O Bus switch below to actually turn WB-MIO off."
  },
  "ru": {
    "baudrate": "Скорость",
    "parity": "Четность",
    "even": "Четный",
    "odd": "Нечетный",
    "stopbits": "Стоп-бит",
    "databits": "Биты данных",
    "failsafe": "Failsafe bias",
    "failsafe_hint": "Резисторы 560 Ω, которые растягивают линии A/B шины, устраняя неопределённость при выключенных передатчиках устройств. Включите, если модуль работает мастером; в остальных случаях выключите.",
    "terminator": "120Ω резистор-терминатор",
    "terminator_hint": "Резистор 120 Ω, подключённый между линиями A и B. \nВключите, если модуль стоит в конце шины; если в середине — выключите.",
    "tx_disabled": "Отключить передачу (TX)",
    "tx_disabled_warning": "Отключение TX сломает режимы моста — полностью работоспособными останутся только сниффер и кэш шины. Мосты будут только передавать данные из RS-485 в TCP, но не смогут отправлять данные из TCP в шину RS-485.",
    "tx_disabled_mio_warning": "WB-MIO не отключается: он остаётся на общей шине RS-485-2 и продолжает отвечать другому мастеру. Доступ теряет только сам шлюз — с отключённым TX он не может передать в шину запросы из TCP; чтобы действительно выключить WB-MIO, используйте переключатель I/O Bus ниже."
  },
  "kk": {
    "baudrate": "Жылдамдық",
    "parity": "Жұптылық",
    "even": "Жұп",
    "odd": "Тақ",
    "stopbits": "Стоп-биттер",
    "databits": "Дерек биттері",
    "failsafe": "Failsafe bias",
    "failsafe_hint": "560 Ω резисторлары A/B желілерін тартып, құрылғылардың таратқыштары өшкенде шинадағы белгісіздікті жояды. Модуль шина мастері ретінде жұмыс істегенде қосыңыз; қалған жағдайларда өшіріп қойыңыз.",
    "terminator": "120Ω терминатор резисторы",
    "terminator_hint": "A және B желілерінің арасына қосылған 120 Ω резистор. \nМодуль шинаның соңында тұрса қосыңыз; ортасында болса өшіріп қойыңыз.",
    "tx_disabled": "Жіберуді өшіру (TX)",
    "tx_disabled_warning": "TX өшіру көпір режимдерін бұзады — тек sniffer және кэш шина толық жұмыс істейді. Көпірлер тек RS-485-тен TCP-ге дерек береді, бірақ TCP-ден RS-485 шинасына жібере алмайды.",
    "tx_disabled_mio_warning": "WB-MIO өшпейді: ол ортақ RS-485-2 шинасында қалады және басқа мастерге жауап беруін жалғастырады. Қолжетімділікті тек шлюздің өзі жоғалтады — TX өшірулі кезде ол TCP-ден келген сұрауларды шинаға жібере алмайды; WB-MIO-ны шынымен өшіру үшін төмендегі I/O Bus ауыстырғышын қолданыңыз."
  },
  "it": {
    "baudrate": "Velocità in baud",
    "parity": "Parità",
    "even": "Pari",
    "odd": "Dispari",
    "stopbits": "Bit di stop",
    "databits": "Bit di dati",
    "failsafe": "Failsafe bias",
    "failsafe_hint": "Resistori di bias da 560 Ω che polarizzano le linee A/B, eliminando lo stato indefinito del bus quando i trasmettitori dei dispositivi sono inattivi. Abilitalo quando il modulo funge da master del bus; altrimenti lascialo disattivato.",
    "terminator": "Resistenza di terminazione 120Ω",
    "terminator_hint": "Resistenza da 120 Ω collegata tra le linee A e B. \nAbilitala quando il modulo si trova all'estremità del bus; se è al centro, lasciala disattivata.",
    "tx_disabled": "Disabilita trasmissione (TX)",
    "tx_disabled_warning": "La disabilitazione di TX interromperà le modalità bridge — solo sniffer e cache bus rimarranno completamente funzionali. I bridge inoltreranno solo i dati da RS-485 a TCP, ma non invieranno dati da TCP al bus RS-485.",
    "tx_disabled_mio_warning": "WB-MIO non viene disattivato: resta sul bus RS-485-2 condiviso e continua a rispondere a un altro master. È solo il gateway a perdere l'accesso — con il TX disabilitato non può inoltrare al bus le richieste dal TCP; per disattivare davvero WB-MIO usa l'interruttore I/O Bus qui sotto."
  },
  "de": {
    "baudrate": "Baudrate",
    "parity": "Parität",
    "even": "Gerade",
    "odd": "Ungerade",
    "stopbits": "Stoppbits",
    "databits": "Datenbits",
    "failsafe": "Failsafe bias",
    "failsafe_hint": "560-Ω-Bias-Widerstände, die die A/B-Leitungen vorspannen und den undefinierten Buszustand beseitigen, wenn die Sender der Geräte inaktiv sind. Aktivieren Sie ihn, wenn das Modul als Bus-Master arbeitet; andernfalls lassen Sie ihn aus.",
    "terminator": "120Ω Abschlusswiderstand",
    "terminator_hint": "120-Ω-Widerstand zwischen den Leitungen A und B. \nAktivieren Sie ihn, wenn sich das Modul am Busende befindet; sitzt es in der Mitte, lassen Sie ihn aus.",
    "tx_disabled": "Senden deaktivieren (TX)",
    "tx_disabled_warning": "Das Deaktivieren von TX unterbricht Bridge-Modi — nur Sniffer und Cache-Bus bleiben vollständig funktionsfähig. Bridges leiten nur Daten von RS-485 zu TCP weiter, senden aber keine Daten vom TCP in den RS-485-Bus.",
    "tx_disabled_mio_warning": "WB-MIO wird nicht abgeschaltet: Es bleibt am gemeinsamen RS-485-2-Bus und antwortet einem anderen Master weiterhin. Nur das Gateway selbst verliert den Zugriff — bei deaktiviertem TX kann es keine Anfragen vom TCP in den Bus weiterleiten; zum tatsächlichen Abschalten von WB-MIO verwenden Sie den Schalter I/O Bus weiter unten."
  }
}
</i18n>
