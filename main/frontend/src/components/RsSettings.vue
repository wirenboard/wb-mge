<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import type { Baudrate, Databits, Parity, RsSettings, Settings, Stopbits } from '@/common/types';
import Button from '@/components/Button.vue';
import Switch from '@/components/Switch.vue';

const props = defineProps<{ title: string; sub?: string; field: string }>();

const { t } = useI18n();
const { isChanged, isLoading, updateSettings } = useSettings();
const settings = defineModel<RsSettings>('settings');
const ioBus = defineModel<boolean>('io_bus', { required: false });

const baudrateOptions: Baudrate[] = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

const parityOptions: Parity[] = ['none', 'even', 'odd'];

const stopBits: Stopbits[] = ['1', '1.5', '2'];

const dataBits: Databits[] = ['5', '6', '7', '8'];

const save = () => {
  const data: Partial<Settings> = {
    [props.field]: settings.value,
  };

  if (typeof ioBus.value !== 'undefined' && props.field === 'rs485_2') {
    data['io_bus'] = ioBus.value;
  }
  updateSettings(data);
};

const isSaveDisabled = computed(() => {
  const fields = props.field === 'rs485_2' ? [props.field, 'io_bus'] : [props.field];
  return isLoading.value || !isChanged(fields);
});
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
        </div>
        <div class="field">
          <label :for="`${field}-term`">{{ t('terminator') }}</label>
          <div class="field-switch"><Switch :id="`${field}-term`" v-model="settings!.term" /></div>
        </div>
        <div class="field">
          <label :for="`${field}-tx_disabled`">{{ t('tx_disabled') }}</label>
          <div class="field-switch"><Switch :id="`${field}-tx_disabled`" v-model="settings!.tx_disabled" /></div>
        </div>
        <div v-if="settings!.tx_disabled" class="field-warning">
          <span class="field-warning-icon">⚠</span>
          {{ t('tx_disabled_warning') }}
        </div>
      </div>
    </form>
  </section>
</template>

<style scoped>
.field-switch {
  justify-self: end;
}

.field-warning {
  display: flex;
  gap: 6px;
  align-items: flex-start;
  font-size: 12px;
  color: var(--warn);
  padding: 6px 0 2px 0;
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
    "terminator": "120Ω termination resistor",
    "tx_disabled": "Disable transmission (TX)",
    "tx_disabled_warning": "Disabling TX will break bridge modes — only sniffer and cache bus will remain fully functional. Bridge connections will only forward data from RS-485 to TCP, but will not send data from TCP into the RS-485 bus."
  },
  "ru": {
    "baudrate": "Скорость",
    "parity": "Четность",
    "even": "Четный",
    "odd": "Нечетный",
    "stopbits": "Стоп-бит",
    "databits": "Биты данных",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω резистор-терминатор",
    "tx_disabled": "Отключить передачу (TX)",
    "tx_disabled_warning": "Отключение TX сломает режимы моста — полностью работоспособными останутся только сниффер и кэш шины. Мосты будут только передавать данные из RS-485 в TCP, но не смогут отправлять данные из TCP в шину RS-485."
  },
  "kk": {
    "baudrate": "Жылдамдық",
    "parity": "Жұптылық",
    "even": "Жұп",
    "odd": "Тақ",
    "stopbits": "Стоп-биттер",
    "databits": "Дерек биттері",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω терминатор резисторы",
    "tx_disabled": "Жіберуді өшіру (TX)",
    "tx_disabled_warning": "TX өшіру көпір режимдерін бұзады — тек sniffer және кэш шина толық жұмыс істейді. Көпірлер тек RS-485-тен TCP-ге дерек береді, бірақ TCP-ден RS-485 шинасына жібере алмайды."
  },
  "it": {
    "baudrate": "Velocità in baud",
    "parity": "Parità",
    "even": "Pari",
    "odd": "Dispari",
    "stopbits": "Bit di stop",
    "databits": "Bit di dati",
    "failsafe": "Failsafe bias",
    "terminator": "Resistenza di terminazione 120Ω",
    "tx_disabled": "Disabilita trasmissione (TX)",
    "tx_disabled_warning": "La disabilitazione di TX interromperà le modalità bridge — solo sniffer e cache bus rimarranno completamente funzionali. I bridge inoltreranno solo i dati da RS-485 a TCP, ma non invieranno dati da TCP al bus RS-485."
  },
  "de": {
    "baudrate": "Baudrate",
    "parity": "Parität",
    "even": "Gerade",
    "odd": "Ungerade",
    "stopbits": "Stoppbits",
    "databits": "Datenbits",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω Abschlusswiderstand",
    "tx_disabled": "Senden deaktivieren (TX)",
    "tx_disabled_warning": "Das Deaktivieren von TX unterbricht Bridge-Modi — nur Sniffer und Cache-Bus bleiben vollständig funktionsfähig. Bridges leiten nur Daten von RS-485 zu TCP weiter, senden aber keine Daten vom TCP in den RS-485-Bus."
  }
}
</i18n>
