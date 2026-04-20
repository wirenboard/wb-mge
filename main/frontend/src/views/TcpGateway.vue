<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import type { BridgeMode, RsSettings } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InputNumber from '@/components/InputNumber.vue';
import IpInput from '@/components/IpInput.vue';
import Layout from '@/components/Layout.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();

const bridgeModbus = computed(() => [
  { value: true, label: t('bridge_modbus') },
  { value: false, label: t('bridge_transparent') },
]);

const ports = ['rs485_1', 'rs485_2'] as const;

const getBridgeMode = (settings: RsSettings): BridgeMode[] =>
  !settings.bridge.modbus ? ['client', 'server'] : ['server'];

const hasPortsConflict = computed(
  () => data.value?.rs485_1.bridge.port === data.value?.rs485_2.bridge.port
);

const save = (field: 'rs485_1' | 'rs485_2') => {
  updateSettings({ [field]: data.value![field] });
};

const isSaveDisabled = (field: 'rs485_1' | 'rs485_2') =>
  isLoading.value || hasPortsConflict.value || !isChanged([field]);

const onModeChange = (ev: Event, portKey: 'rs485_1' | 'rs485_2') => {
  const target = ev.target as HTMLSelectElement;
  if (target.value === 'true') {
    data.value![portKey].bridge.mode = 'server';
  }
};

const portSubs = ['port1_sub', 'port2_sub'];
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <section v-for="(portKey, idx) in ports" :key="portKey" class="card">
          <form @submit.prevent="save(portKey)">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">RS-485 · Port {{ idx + 1 }}</div>
                <div class="sub">{{ t(portSubs[idx]) }}</div>
              </div>
              <Button
                type="submit"
                :is-loading="isLoading"
                :disabled="isSaveDisabled(portKey)"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label :for="`${portKey}-bridge_mb`">{{ t('modbus_mode') }}</label>
                <select
                  :id="`${portKey}-bridge_mb`"
                  v-model="data[portKey].bridge.modbus"
                  name="bridge_mb"
                  @change="onModeChange($event, portKey)"
                >
                  <option v-for="item in bridgeModbus" :key="`bridge_mb_${portKey}_${item.value}`" :value="item.value">{{ item.label }}</option>
                </select>
              </div>
              <div class="field">
                <label :for="`${portKey}-bridge_mode`">{{ t('bridge_mode') }}</label>
                <select :id="`${portKey}-bridge_mode`" v-model="data[portKey].bridge.mode" :disabled="data[portKey].bridge.modbus" name="bridge_mode">
                  <option v-for="item in getBridgeMode(data[portKey])" :key="`bridge_mode_${portKey}_${item}`" :value="item">{{ t(item) }}</option>
                </select>
              </div>
              <template v-if="data[portKey].bridge.mode !== 'server'">
                <div class="field">
                  <label :for="`${portKey}-bridge_ip`">{{ t('bridge_ip') }}</label>
                  <IpInput :id="`${portKey}-bridge_ip`" v-model="data[portKey].bridge.ip" name="bridge_ip" />
                </div>
              </template>
              <div class="field">
                <label :for="`${portKey}-bridge_port`">{{ t('port') }}</label>
                <InputNumber
                  :id="`${portKey}-bridge_port`"
                  v-model="data[portKey].bridge.port"
                  name="bridge_port"
                  min="1"
                  max="65535"
                  class="tcpGateway-port mono"
                  :invalid="hasPortsConflict"
                  required
                />
              </div>
              <Info v-if="isChanged([portKey]) && hasPortsConflict" :text="t('ports_conflict')" severity="error" />
            </div>
          </form>
        </section>
      </div>
    </div>
  </Layout>
</template>

<style>
.tcpGateway-port {
  max-width: 85px;
}
</style>

<i18n>
{
  "en": {
    "title": "TCP gateway",
    "crumbs": "TCP gateway settings",
    "port1_sub": "Wired terminal · left",
    "port2_sub": "Wired terminal · right + I/O bus",
    "modbus_mode": "Modbus mode",
    "bridge_mode": "Bridge mode",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent",
    "bridge_ip": "IP address",
    "ports_conflict": "Port values must be unique",
    "save": "Save"
  },
  "ru": {
    "title": "TCP-шлюз",
    "crumbs": "Настройки TCP-шлюза",
    "port1_sub": "Клеммник · левый",
    "port2_sub": "Клеммник · правый + I/O bus",
    "modbus_mode": "Режим",
    "bridge_mode": "Роль",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Прозрачный",
    "bridge_ip": "IP-адрес сервера",
    "ports_conflict": "Значение порта должно быть уникальным",
    "save": "Сохранить"
  },
  "kk": {
    "title": "TCP шлюзі",
    "crumbs": "TCP шлюзінің баптаулары",
    "port1_sub": "Сымды клемма · сол",
    "port2_sub": "Сымды клемма · оң + I/O bus",
    "modbus_mode": "Режим",
    "bridge_mode": "Рөл",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Мөлдір",
    "bridge_ip": "IP мекенжайы",
    "ports_conflict": "Порт мәндері бірегей болуы керек",
    "save": "Сақтау"
  },
  "it": {
    "title": "Gateway TCP",
    "crumbs": "Impostazioni gateway TCP",
    "port1_sub": "Morsettiera · sinistra",
    "port2_sub": "Morsettiera · destra + I/O bus",
    "modbus_mode": "Modalità Modbus",
    "bridge_mode": "Modalità bridge",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Trasparente",
    "bridge_ip": "Indirizzo IP",
    "ports_conflict": "I valori delle porte devono essere unici",
    "save": "Salva"
  },
  "de": {
    "title": "TCP-Gateway",
    "crumbs": "TCP-Gateway Einstellungen",
    "port1_sub": "Klemmenleiste · links",
    "port2_sub": "Klemmenleiste · rechts + I/O bus",
    "modbus_mode": "Modbus-Modus",
    "bridge_mode": "Bridge-Modus",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent",
    "bridge_ip": "IP-Adresse",
    "ports_conflict": "Portwerte müssen eindeutig sein",
    "save": "Speichern"
  }
}
</i18n>
