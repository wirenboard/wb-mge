<script setup lang="ts">
import { computed, type ComputedRef } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import type { Baudrate, BridgeMode, Databits, Parity, RsSettings, Settings, Stopbits } from '@/common/types';
import Button from '@/components/Button.vue';
import Info from '@/components/Info.vue';
import InputNumber from '@/components/InputNumber.vue';
import IpInput from '@/components/IpInput.vue';
import Switch from '@/components/Switch.vue';

const props = defineProps<{ title: string; sub?: string; field: string; hasPortsConflict: boolean }>();

const { t } = useI18n();
const { isChanged, isLoading, updateSettings } = useSettings();
const settings = defineModel<RsSettings>('settings');
const ioBus = defineModel<boolean>('io_bus', { required: false });

const baudrateOptions: Baudrate[] = [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200];

const parityOptions: Parity[] = ['none', 'even', 'odd'];

const stopBits: Stopbits[] = ['1', '1.5', '2'];

const dataBits: Databits[] = ['5', '6', '7', '8'];

const bridgeModbus = [{ value: true, label: t('bridge_modbus') }, { value: false, label:  t('bridge_transparent') }];

const bridgeMode: ComputedRef<BridgeMode[]> = computed(() => !settings.value!.bridge.modbus ? ['client', 'server'] : ['server']);

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
  return isLoading.value || props.hasPortsConflict || !isChanged(fields);
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
        <div class="sub-section">
          <div class="sub-section-label">{{ t('serial_section') }}</div>
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
          <template v-if="field === 'rs485_2'">
            <div class="field">
              <label :for="`${field}-io_bus`">{{ t('io_bus') }}</label>
              <div style="justify-self: end"><Switch :id="`${field}-io_bus`" v-model="ioBus" /></div>
              <div class="hint">{{ t('io_bus_info') }}</div>
            </div>
          </template>
        </div>

        <div class="sub-section">
          <div class="sub-section-label">{{ t('tcp_section') }}</div>
          <div class="field">
            <label :for="`${field}-bridge_mb`">{{ t('modbus_mode') }}</label>
            <select
              :id="`${field}-bridge_mb`"
              v-model="settings!.bridge.modbus"
              name="bridge_mb"
              @change="(ev: Event) => {
                const target = ev.target as HTMLSelectElement;
                if (target.value === 'true') {
                  settings!.bridge.mode = 'server';
                }
              }">
              <option v-for="item in bridgeModbus" :key="`bridge_mb_1${item}`" :value="item.value">{{ item.label }}</option>
            </select>
          </div>
          <div class="field">
            <label :for="`${field}-bridge_mode`">{{ t('bridge_mode') }}</label>
            <select :id="`${field}-bridge_mode`" v-model="settings!.bridge.mode" :disabled="settings!.bridge.modbus" name="bridge_mode">
              <option v-for="item in bridgeMode" :key="`bridge_mode_1${item}`" :value="item">{{ t(item) }}</option>
            </select>
          </div>
          <template v-if="settings!.bridge.mode !== 'server'">
            <div class="field">
              <label :for="`${field}-bridge_ip`">{{ t('bridge_ip') }}</label>
              <IpInput :id="`${field}-bridge_ip`" v-model="settings!.bridge.ip" name="bridge_ip" />
            </div>
          </template>
          <div class="field">
            <label :for="`${field}-bridge_port`">{{ t('port') }}</label>
            <InputNumber
              :id="`${field}-bridge_port`"
              v-model="settings!.bridge.port"
              name="bridge_port"
              min="1"
              max="65535"
              class="rsSettings-port mono"
              :invalid="hasPortsConflict"
              required
            />
          </div>
          <Info v-if="isChanged([field, 'io_bus']) && hasPortsConflict" :text="t('ports_conflict')" severity="error" />
        </div>
      </div>
    </form>
  </section>
</template>

<style>
.rsSettings-port {
  max-width: 85px;
}
</style>

<i18n>
{
  "en": {
    "serial_section": "Serial",
    "tcp_section": "TCP gateway",
    "baudrate": "Baud rate",
    "parity": "Parity",
    "even": "Even",
    "odd": "Odd",
    "stopbits": "Stop bits",
    "databits": "Data bits",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω termination resistor",
    "modbus_mode": "Modbus mode",
    "bridge_mode": "Bridge mode",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent",
    "bridge_ip": "IP address",
    "io_bus": "I/O Bus",
    "io_bus_info": "Enables WB-MIO chip connected to RS485-2.\nDefault address 247",
    "ports_conflict": "Port values must be unique"
  },
  "ru": {
    "serial_section": "Последовательный порт",
    "tcp_section": "TCP-шлюз",
    "baudrate": "Скорость",
    "parity": "Четность",
    "even": "Четный",
    "odd": "Нечетный",
    "stopbits": "Стоп-бит",
    "databits": "Биты данных",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω резистор-терминатор",
    "modbus_mode": "Режим",
    "bridge_mode": "Роль",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Прозрачный",
    "bridge_ip": "IP-адрес сервера",
    "io_bus": "I/O Bus",
    "io_bus_info": "Включает чип WB-MIO, подключенный к RS485-2.\nАдрес по умолчанию 247",
    "ports_conflict": "Значение порта должно быть уникальным"
  },
  "kk": {
    "serial_section": "Сериялық порт",
    "tcp_section": "TCP шлюзі",
    "baudrate": "Жылдамдық",
    "parity": "Жұптылық",
    "even": "Жұп",
    "odd": "Тақ",
    "stopbits": "Стоп-биттер",
    "databits": "Дерек биттері",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω терминатор резисторы",
    "modbus_mode": "Режим",
    "bridge_mode": "Рөл",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Мөлдір",
    "bridge_ip": "IP мекенжайы",
    "io_bus": "I/O Bus",
    "io_bus_info": "RS485-2-ге қосылған WB-MIO чипін қосады.\nӘдепкі адресі 247",
    "ports_conflict": "Порт мәндері бірегей болуы керек"
  },
  "it": {
    "serial_section": "Seriale",
    "tcp_section": "Gateway TCP",
    "baudrate": "Velocità in baud",
    "parity": "Parità",
    "even": "Pari",
    "odd": "Dispari",
    "stopbits": "Bit di stop",
    "databits": "Bit di dati",
    "failsafe": "Failsafe bias",
    "terminator": "Resistenza di terminazione 120Ω",
    "modbus_mode": "Modalità Modbus",
    "bridge_mode": "Modalità bridge",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Trasparente",
    "bridge_ip": "Indirizzo IP",
    "io_bus": "I/O Bus",
    "io_bus_info": "Abilita il chip WB-MIO collegato a RS485-2.\nIndirizzo predefinito 247",
    "ports_conflict": "I valori delle porte devono essere unici"
  },
  "de": {
    "serial_section": "Seriell",
    "tcp_section": "TCP-Gateway",
    "baudrate": "Baudrate",
    "parity": "Parität",
    "even": "Gerade",
    "odd": "Ungerade",
    "stopbits": "Stoppbits",
    "databits": "Datenbits",
    "failsafe": "Failsafe bias",
    "terminator": "120Ω Abschlusswiderstand",
    "modbus_mode": "Modbus-Modus",
    "bridge_mode": "Bridge-Modus",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent",
    "bridge_ip": "IP-Adresse",
    "io_bus": "I/O Bus",
    "io_bus_info": "Aktiviert den an RS485-2 angeschlossenen WB-MIO-Chip.\nStandardadresse 247",
    "ports_conflict": "Portwerte müssen eindeutig sein"
  }
}
</i18n>
