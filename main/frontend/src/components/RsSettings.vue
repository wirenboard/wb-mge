<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import type { Baudrate, BridgeMode, Databits, Parity, RsSettings, Stopbits } from '@/common/types';
import Button from '@/components/Button.vue';
import Info from '@/components/Info.vue';
import InputNumber from '@/components/InputNumber.vue';
import IpInput from '@/components/IpInput.vue';
import Switch from '@/components/Switch.vue';

const props = defineProps<{ title: string; field: string; hasPortsConflict: boolean }>();

const { t } = useI18n();
const { isChanged, isLoading, updateSettings } = useSettings();
const settings = defineModel<RsSettings>('settings');
const ioBus = defineModel('io_bus', { required: false });

const baudrateOptions: Baudrate[] = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

const parityOptions: Parity[] = ['none', 'even', 'odd'];

const stopBits: Stopbits[] = ['1', '1.5', '2'];

const dataBits: Databits[] = ['5', '6', '7', '8'];

const bridgeModbus = [{ value: true, label: t('bridge_modbus') }, { value: false, label:  t('bridge_transparent') }];

const bridgeMode: BridgeMode[] = ['client', 'server'];

const save = () => {
  const data: any = {
    [props.field]: settings.value,
  };

  if (typeof ioBus.value !== 'undefined') {
    data['io_bus'] = ioBus.value;
  }
  debugger;
  updateSettings(data);
};
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

      <template v-if="typeof ioBus === 'boolean'">
        <label :for="`${field}-io_bus`">{{ t('io_bus') }}</label>
        <div class="settings-data">
          <Switch
            :id="`${field}-io_bus`"
            v-model="ioBus"
          />
        </div>
        <Info :text="t('io_bus_info')" />
      </template>

      <b>TCP</b>
      <div></div>

      <label :for="`${field}-bridge_mb`">{{ t('modbus_mode') }}</label>
      <div class="settings-data">
        <select :id="`${field}-bridge_mb`" v-model="settings!.bridge.modbus" name="bridge_mb">
          <option v-for="item in bridgeModbus" :key="`bridge_mb_1${item}`" :value="item.value">{{ item.label }}</option>
        </select>
      </div>

      <label :for="`${field}-bridge_mode`">{{ t('bridge_mode') }}</label>
      <div class="settings-data">
        <select :id="`${field}-bridge_mode`" v-model="settings!.bridge.mode" name="bridge_mode">
          <option v-for="item in bridgeMode" :key="`bridge_mode_1${item}`" :value="item">{{ t(item) }}</option>
        </select>
      </div>

      <template v-if="settings!.bridge.mode !== 'server'">
        <label :for="`${field}-bridge_ip`">{{ t('bridge_ip') }}</label>
        <div class="settings-data">
          <IpInput :id="`${field}-bridge_ip`" v-model="settings!.bridge.ip" name="bridge_ip" />
        </div>
      </template>

      <label :for="`${field}-bridge_port`">{{ t('port') }}</label>
      <div class="settings-data">
        <InputNumber
          :id="`${field}-bridge_port`"
          v-model="settings!.bridge.port"
          name="bridge_port"
          min="1"
          max="65535"
          class="rsSettings-port"
          :invalid="hasPortsConflict"
          required
        />
      </div>
      <Info v-if="isChanged([field, 'io_bus']) && hasPortsConflict" :text="t('ports_conflict')" severity="error" />

      <Button
        class="settings-submit"
        type="submit"
        :is-loading="isLoading"
        :disabled="isLoading || !isChanged([field, 'io_bus']) || hasPortsConflict"
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
    "terminator": "Terminator",
    "modbus_mode": "Modbus mode",
    "bridge_mode": "Bridge mode",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent",
    "bridge_ip": "IP address",
    "io_bus": "I/O Bus",
    "io_bus_info": "Adds RS-485 support for WB-MIO side modules",
    "ports_conflict": "Port values must be unique"
  },
  "ru": {
    "baudrate": "Скорость",
    "parity": "Четность",
    "even": "Четный",
    "odd": "Нечетный",
    "stopbits": "Стоп-бит",
    "databits": "Биты данных",
    "failsafe": "Failsafe bias",
    "terminator": "Терминатор",
    "modbus_mode": "Режим",
    "bridge_mode": "Роль",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Прозрачный",
    "bridge_ip": "IP-адрес сервера",
    "io_bus": "Боковые модули",
    "io_bus_info": "Добавляет поддержку боковых модулей WB-MIO по RS-485",
    "ports_conflict": "Значение порта должно быть уникальным"
  }
}
</i18n>
