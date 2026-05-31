<script setup lang="ts">
import { computed, type WritableComputedRef } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import { useInfo } from '@/common/info';
import { useAlerts } from '@/common/alert';
import { useOptimisticToggle } from '@/common/useOptimisticToggle';
import { api } from '@/utils/api';
import type { BridgeMode, RsSettings } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InputNumber from '@/components/InputNumber.vue';
import IpInput from '@/components/IpInput.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
const { info } = useInfo();
const { showAlert } = useAlerts();

type PortKey = 'rs485_1' | 'rs485_2';

// 1-based port number used by the backend ports/<N>/mode endpoint
const portNumber: Record<PortKey, 1 | 2> = { rs485_1: 1, rs485_2: 2 };

// One optimistic-toggle state machine per port. The TCP gateway is considered ON for a port
// when its transport mode is 'tcp_bridge'; on a failed toggle we surface the connection alert.
const toggles: Record<PortKey, ReturnType<typeof useOptimisticToggle>> = {
  rs485_1: useOptimisticToggle({
    derive: () => info.value?.rs485_1.port_mode === 'tcp_bridge',
    onError: () => showAlert('connection_error'),
  }),
  rs485_2: useOptimisticToggle({
    derive: () => info.value?.rs485_2.port_mode === 'tcp_bridge',
    onError: () => showAlert('connection_error'),
  }),
};

// The displayed enable state for a port (optimistic override if set, else derived from info).
const isEnabled = (portKey: PortKey): boolean => toggles[portKey].value.value;

// True while a toggle request is in flight or info has not loaded yet.
const isToggleDisabled = (portKey: PortKey): boolean =>
  toggles[portKey].inFlight.value || info.value === undefined;

// Toggle the TCP gateway transport mode for a single port.
// ON  -> open as 'tcp_bridge'.
// OFF -> 'passive' when the cache overlay is active (keep serial open for the
//        Register Map cache listener), otherwise 'disabled' (fully off).
function toggleEnabled(portKey: PortKey): void {
  if (info.value === undefined) return; // cannot determine target state yet
  const n = portNumber[portKey];
  toggles[portKey].run(async (wasEnabled) => {
    if (wasEnabled) {
      // Turning the gateway off: keep serial alive as 'passive' when the cache overlay
      // is on, otherwise close the port fully with 'disabled'.
      const cacheOn = info.value![portKey].cache_enabled;
      const mode = cacheOn ? 'passive' : 'disabled';
      await api<void>(`ports/${n}/mode`, { method: 'POST', json: { mode } });
    } else {
      await api<void>(`ports/${n}/mode`, { method: 'POST', json: { mode: 'tcp_bridge' } });
    }
  });
}

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
const portTitles = ['port_1', 'port_2'] as const;

// Writable v-model bridges for the enable Switch, one stable computed per port. get returns
// the current enabled state (optimistic override if set, else derived from info); set ignores
// the incoming value and flips based on the current state via the toggle handler.
const enabledModel: Record<PortKey, WritableComputedRef<boolean>> = {
  rs485_1: computed<boolean>({
    get: () => isEnabled('rs485_1'),
    set: () => {
      toggleEnabled('rs485_1');
    },
  }),
  rs485_2: computed<boolean>({
    get: () => isEnabled('rs485_2'),
    set: () => {
      toggleEnabled('rs485_2');
    },
  }),
};
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
                <div class="title">{{ t(portTitles[idx]) }}</div>
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
                <label :for="`${portKey}-enabled`">{{ t('enabled') }}</label>
                <div class="field-switch">
                  <Switch
                    :id="`${portKey}-enabled`"
                    v-model="enabledModel[portKey].value"
                    :aria-label="t('enabled')"
                    :disabled="isToggleDisabled(portKey)"
                  />
                </div>
              </div>
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
              <div v-if="!data[portKey].bridge.modbus" class="field">
                <label :for="`${portKey}-bridge_mode`">{{ t('bridge_mode') }}</label>
                <select :id="`${portKey}-bridge_mode`" v-model="data[portKey].bridge.mode" name="bridge_mode">
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

<style scoped>
.tcpGateway-port {
  max-width: 85px;
  justify-self: end; /* align input to the right edge of the field grid column */
}

.field-switch {
  justify-self: end; /* align the switch to the right edge of the field grid column */
}
</style>

<i18n>
{
  "en": {
    "title": "TCP gateway",
    "crumbs": "TCP gateway settings",
    "port1_sub": "Wired terminal · left",
    "port2_sub": "Wired terminal · right + I/O bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "enabled": "Enabled",
    "modbus_mode": "Mode",
    "bridge_mode": "Bridge mode",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent bridge",
    "bridge_ip": "IP address",
    "ports_conflict": "Port values must be unique",
    "save": "Save"
  },
  "ru": {
    "title": "TCP-шлюз",
    "crumbs": "Настройки TCP-шлюза",
    "port1_sub": "Левый клеммник",
    "port2_sub": "Правый клеммник + I/O bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "enabled": "Включён",
    "modbus_mode": "Режим",
    "bridge_mode": "Роль",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Прозрачный мост",
    "bridge_ip": "IP-адрес сервера",
    "ports_conflict": "Значение порта должно быть уникальным",
    "save": "Сохранить"
  },
  "kk": {
    "title": "TCP шлюзі",
    "crumbs": "TCP шлюзінің баптаулары",
    "port1_sub": "Сымды клемма · сол",
    "port2_sub": "Сымды клемма · оң + I/O bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "enabled": "Қосулы",
    "modbus_mode": "Режим",
    "bridge_mode": "Рөл",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Мөлдір көпір",
    "bridge_ip": "IP мекенжайы",
    "ports_conflict": "Порт мәндері бірегей болуы керек",
    "save": "Сақтау"
  },
  "it": {
    "title": "Gateway TCP",
    "crumbs": "Impostazioni gateway TCP",
    "port1_sub": "Morsettiera · sinistra",
    "port2_sub": "Morsettiera · destra + I/O bus",
    "port_1": "RS-485 · Porta 1",
    "port_2": "RS-485 · Porta 2",
    "enabled": "Abilitato",
    "modbus_mode": "Modalità",
    "bridge_mode": "Modalità bridge",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Bridge trasparente",
    "bridge_ip": "Indirizzo IP",
    "ports_conflict": "I valori delle porte devono essere unici",
    "save": "Salva"
  },
  "de": {
    "title": "TCP-Gateway",
    "crumbs": "TCP-Gateway Einstellungen",
    "port1_sub": "Klemmenleiste · links",
    "port2_sub": "Klemmenleiste · rechts + I/O bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "enabled": "Aktiviert",
    "modbus_mode": "Modus",
    "bridge_mode": "Bridge-Modus",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparente Brücke",
    "bridge_ip": "IP-Adresse",
    "ports_conflict": "Portwerte müssen eindeutig sein",
    "save": "Speichern"
  }
}
</i18n>
