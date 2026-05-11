<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import type { Baudrate, Databits, Parity, RsSettings, Settings, Stopbits } from '@/common/types';
import Button from '@/components/Button.vue';
import Info from '@/components/Info.vue';
import Switch from '@/components/Switch.vue';

const props = defineProps<{ title: string; field: string }>();

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
  <fieldset>
    <legend>{{ title }}</legend>
    <form
      class="settings-info"
      @submit.prevent="save">
      <label :for="`${field}-baudrate`">{{ t('baudrate') }}</label>
      <div class="settings-data">
        <select :id="`${field}-baudrate`" v-model="settings!.baudrate" name="baudrate">
          <option v-for="item in baudrateOptions" :key="`baudrate_1_${item}`" :value="item">{{ item }}</option>
        </select>
      </div>

      <label :for="`${field}-parity`">{{ t('parity') }}</label>
      <div class="settings-data">
        <select :id="`${field}-parity`" v-model="settings!.parity" name="parity">
          <option v-for="item in parityOptions" :key="`parity_1_${item}`" :value="item">{{ t(item) }}</option>
        </select>
      </div>

      <label :for="`${field}-stopbits`">{{ t('stopbits') }}</label>
      <div class="settings-data">
        <select :id="`${field}-stopbits`" v-model="settings!.stopbits" name="stopbits">
          <option v-for="item in stopBits" :key="`stopbits_1_${item}`" :value="item">{{ item?.split('-')[0] }}</option>
        </select>
      </div>

      <label :for="`${field}-databits`">{{ t('databits') }}</label>
      <div class="settings-data">
        <select :id="`${field}-databits`" v-model="settings!.databits" name="databits">
          <option v-for="item in dataBits" :key="`stopbits_1_${item}`" :value="item">{{ item?.split('-')[0] }}</option>
        </select>
      </div>

      <label :for="`${field}-fail_safe`">{{ t('failsafe') }}</label>
      <div class="settings-data">
        <Switch
          :id="`${field}-fail_safe`"
          v-model="settings!.fail_safe"
        />
      </div>

      <label :for="`${field}-term`">{{ t('terminator') }}</label>
      <div class="settings-data">
        <Switch
          :id="`${field}-term`"
          v-model="settings!.term"
        />
      </div>

      <template v-if="field === 'rs485_2'">
        <label :for="`${field}-io_bus`">{{ t('io_bus') }}</label>
        <div class="settings-data">
          <Switch
            :id="`${field}-io_bus`"
            v-model="ioBus"
          />
        </div>
        <Info :text="t('io_bus_info')" />
      </template>

      <Button
        class="settings-submit"
        type="submit"
        :is-loading="isLoading"
        :disabled="isSaveDisabled"
      >
        {{ t('save') }}
      </Button>
    </form>
  </fieldset>
</template>

<style>
.rsSettings-port {
  max-width: 85px;
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
    "io_bus": "I/O Bus",
    "io_bus_info": "Enables WB-MIO chip connected to RS485-2.\nDefault address 247"
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
    "io_bus": "I/O Bus",
    "io_bus_info": "Включает чип WB-MIO, подключенный к RS485-2.\nАдрес по умолчанию 247"
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
    "io_bus": "I/O Bus",
    "io_bus_info": "RS485-2-ге қосылған WB-MIO чипін қосады.\nӘдепкі адресі 247"
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
    "io_bus": "I/O Bus",
    "io_bus_info": "Abilita il chip WB-MIO collegato a RS485-2.\nIndirizzo predefinito 247"
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
    "io_bus": "I/O Bus",
    "io_bus_info": "Aktiviert den an RS485-2 angeschlossenen WB-MIO-Chip.\nStandardadresse 247"
  }
}
</i18n>
