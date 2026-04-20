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
          <div style="justify-self: end"><Switch :id="`${field}-fail_safe`" v-model="settings!.fail_safe" /></div>
        </div>
        <div class="field">
          <label :for="`${field}-term`">{{ t('terminator') }}</label>
          <div style="justify-self: end"><Switch :id="`${field}-term`" v-model="settings!.term" /></div>
        </div>
      </div>
    </form>
  </section>
</template>

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
    "terminator": "120Ω termination resistor"
  },
  "ru": {
    "baudrate": "Скорость",
    "parity": "Четность",
    "even": "Четный",
    "odd": "Нечетный",
    "stopbits": "Стоп-бит",
    "databits": "Биты данных",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω резистор-терминатор"
  },
  "kk": {
    "baudrate": "Жылдамдық",
    "parity": "Жұптылық",
    "even": "Жұп",
    "odd": "Тақ",
    "stopbits": "Стоп-биттер",
    "databits": "Дерек биттері",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω терминатор резисторы"
  },
  "it": {
    "baudrate": "Velocità in baud",
    "parity": "Parità",
    "even": "Pari",
    "odd": "Dispari",
    "stopbits": "Bit di stop",
    "databits": "Bit di dati",
    "failsafe": "Failsafe bias",
    "terminator": "Resistenza di terminazione 120Ω"
  },
  "de": {
    "baudrate": "Baudrate",
    "parity": "Parität",
    "even": "Gerade",
    "odd": "Ungerade",
    "stopbits": "Stoppbits",
    "databits": "Datenbits",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω Abschlusswiderstand"
  }
}
</i18n>
