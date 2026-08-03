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
const { info, fetchInfo } = useInfo();
const { showAlert } = useAlerts();

type PortKey = 'rs485_1' | 'rs485_2';

// 1-based port number used by the backend ports/<N>/mode endpoint
const portNumber: Record<PortKey, 1 | 2> = { rs485_1: 1, rs485_2: 2 };

// The other port of the pair. The repeater always spans BOTH ports, so enabling the gateway
// on one port has to deal with the repeater that is still set on the other one.
const peerOf: Record<PortKey, PortKey> = { rs485_1: 'rs485_2', rs485_2: 'rs485_1' };

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

// True while a toggle request is in flight on EITHER port. The ON branch below writes BOTH
// ports (it takes the peer out of 'repeater'), so the per-port guard inside useOptimisticToggle
// is no longer enough: it would let a click on port 2 run while port 1's sequence is still
// mid-flight, with both toggles writing the same pair of ports from two different snapshots.
const isAnyToggleInFlight = computed<boolean>(
  () => toggles.rs485_1.inFlight.value || toggles.rs485_2.inFlight.value,
);

// True while any toggle request is in flight or info has not loaded yet. Not per-port: a
// toggle on one port must lock the other port's switch as well (see isAnyToggleInFlight).
const isToggleDisabled = computed<boolean>(
  () => isAnyToggleInFlight.value || info.value === undefined,
);

// True when the port currently acts as a transparent repeater. While repeater mode is on,
// the enable toggle derives OFF (it only tracks 'tcp_bridge'), so the banner has to say what
// the click will do instead: enabling the gateway turns the repeater off, and the new mode is
// persisted to NVS (POST /ports/N/mode applies AND saves it), so the repeater setting made on
// the Repeater page is gone for good. The banner needs no state of its own: it clears as soon
// as the port leaves 'repeater' - from the Repeater page, or when toggleEnabled() below opens
// this port as 'tcp_bridge', or when it takes this port out as the peer of the port enabled.
const isRepeaterMode = (portKey: PortKey): boolean =>
  info.value?.[portKey].port_mode === 'repeater';

// Toggle the TCP gateway transport mode for a single port.
// ON  -> take the peer out of 'repeater' first (see below), then open this port as 'tcp_bridge'.
// OFF -> 'passive' when the cache overlay is active (keep serial open for the
//        Register Map cache listener), otherwise 'disabled' (fully off).
function toggleEnabled(portKey: PortKey): void {
  if (info.value === undefined) return; // cannot determine target state yet
  // Cross-port guard. useOptimisticToggle.run() already refuses a second toggle on the SAME
  // port, but the ON branch writes the peer too, so a toggle in flight anywhere on this pair
  // has to block this one. The switch is disabled for the same reason (isToggleDisabled), but
  // that only hides the control - a change event dispatched at it still reaches this handler.
  if (isAnyToggleInFlight.value) return;
  const n = portNumber[portKey];
  toggles[portKey].run(async (wasEnabled) => {
    if (wasEnabled) {
      // Turning the gateway off: keep serial alive as 'passive' when the cache overlay
      // is on, otherwise close the port fully with 'disabled'.
      const cacheOn = info.value![portKey].cache_enabled;
      const mode = cacheOn ? 'passive' : 'disabled';
      await api<void>(`ports/${n}/mode`, { method: 'POST', json: { mode } });
    } else {
      // Turning the gateway on. The repeater is a PAIR: switching only this port to
      // 'tcp_bridge' would leave the peer alone in 'repeater', where it forwards nothing
      // (repeater_rx_handler drops every frame once the peer descriptor is NULL) and where it
      // keeps showing the repeater banner. So take the peer out of 'repeater' as well, using
      // the same target as the OFF branch above but read off the PEER: 'passive' when the
      // PEER's cache overlay is on (keep its serial open for the cache listener), otherwise
      // 'disabled'. A peer in any other mode is deliberately left untouched.
      //
      // Re-read the device state first. This is the only decision on this page that acts on a
      // port the user did not click, and the cached `info` is up to 5 s old: it is refreshed by
      // the poll and by the fire-and-forget fetchInfo('low') that useOptimisticToggle runs after
      // a toggle, neither of which is awaited here. Deciding from that stale copy is what let a
      // second click post 'disabled' to the port the first click had just opened as a gateway -
      // the cache still showed both ports in 'repeater'. Same precedent (and same graceful
      // degradation on a failed fetch) as Sniffer.vue's startCapture().
      try {
        await fetchInfo();
      } catch {
        // Fetch failed: fall back to whatever is cached rather than abandoning the toggle.
      }
      // `info` is a shared ref that the fetch above - and the 5 s poll - can replace at any
      // await, so snapshot it once and read both peer fields off that snapshot. An undefined
      // snapshot cannot happen after the guard at the top of toggleEnabled(), but reading it
      // through `?.` instead of asserting it away degrades to "leave the peer alone" rather
      // than throwing, which is the safe direction for a write to a port nobody clicked.
      const fresh = info.value;
      const peer = peerOf[portKey];
      const peerState = fresh?.[peer];
      const peerInRepeater = peerState?.port_mode === 'repeater';
      const peerMode = peerState?.cache_enabled ? 'passive' : 'disabled';
      // Peer first, sequentially. The order does not remove the half-configured state, it moves
      // which port can be caught in it: a failed SECOND request leaves a port that was itself in
      // 'repeater' alone there, the mirror image of the defect this branch exists to prevent.
      // What it buys is that the click stays repairable by repeating it. A failed peer request
      // sends nothing else and changes nothing on the device; a failed gateway request leaves
      // the switch reading OFF, so the same click retries - and now sends only this port, the
      // peer having already left 'repeater'. Reversed, a failed peer request strands the peer
      // while the switch reads ON, so repeating the click takes the OFF branch and tears the
      // gateway down instead of finishing the job. A parallel Promise.all sends both requests
      // regardless, so a failed peer request can coexist with a successful gateway one - the
      // stranded-peer state this branch exists to prevent.
      if (peerInRepeater) {
        await api<void>(`ports/${portNumber[peer]}/mode`, { method: 'POST', json: { mode: peerMode } });
      }
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

// A port opens a local TCP listener only when it acts as a server: transparent-bridge
// 'server' mode, or Modbus TCP (which is always a server). A 'client' port instead connects
// out to a remote host, so its port number never collides with the other port's.
const isBridgeServer = (settings?: RsSettings): boolean =>
  !!settings && (settings.bridge.modbus || settings.bridge.mode === 'server');

// Only a real clash — two server listeners bound to the same local TCP port — is a conflict.
const hasPortsConflict = computed(() => {
  const p1 = data.value?.rs485_1;
  const p2 = data.value?.rs485_2;
  if (!p1 || !p2) return false;
  return isBridgeServer(p1) && isBridgeServer(p2) && p1.bridge.port === p2.bridge.port;
});

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
              <Info v-if="isRepeaterMode(portKey)" :text="t('repeater_active')" />
              <div class="field">
                <label :for="`${portKey}-enabled`">{{ t('enabled') }}</label>
                <div class="field-switch">
                  <Switch
                    :id="`${portKey}-enabled`"
                    v-model="enabledModel[portKey].value"
                    :aria-label="t('enabled')"
                    :disabled="isToggleDisabled"
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
    "port": "Port",
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
    "ports_conflict": "Two ports in server mode must use different port numbers",
    "repeater_active": "Enabling the TCP gateway turns off the repeater currently active on this port, and that change is saved on the device permanently.",
    "save": "Save"
  },
  "ru": {
    "title": "TCP-шлюз",
    "crumbs": "Настройки TCP-шлюза",
    "port": "Порт",
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
    "ports_conflict": "Два порта в режиме «Сервер» должны использовать разные номера порта",
    "repeater_active": "Включение TCP-шлюза выключит повторитель, который сейчас работает на этом порту, и это изменение будет безвозвратно сохранено в устройстве.",
    "save": "Сохранить"
  },
  "kk": {
    "title": "TCP шлюзі",
    "crumbs": "TCP шлюзінің баптаулары",
    "port": "Порт",
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
    "ports_conflict": "Сервер режиміндегі екі порт әртүрлі порт нөмірлерін қолдануы керек",
    "repeater_active": "TCP шлюзін қосу осы портта қазір жұмыс істеп тұрған қайталағышты өшіреді және бұл өзгеріс құрылғыда біржола сақталады.",
    "save": "Сақтау"
  },
  "it": {
    "title": "Gateway TCP",
    "crumbs": "Impostazioni gateway TCP",
    "port": "Porta",
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
    "ports_conflict": "Due porte in modalità server devono usare numeri di porta diversi",
    "repeater_active": "L'attivazione del gateway TCP disattiva il ripetitore attualmente attivo su questa porta e la modifica viene salvata sul dispositivo in modo permanente.",
    "save": "Salva"
  },
  "de": {
    "title": "TCP-Gateway",
    "crumbs": "TCP-Gateway Einstellungen",
    "port": "Port",
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
    "ports_conflict": "Zwei Ports im Servermodus müssen unterschiedliche Portnummern verwenden",
    "repeater_active": "Das Aktivieren des TCP-Gateways schaltet den derzeit an diesem Port aktiven Repeater aus, und diese Änderung wird dauerhaft auf dem Gerät gespeichert.",
    "save": "Speichern"
  }
}
</i18n>
