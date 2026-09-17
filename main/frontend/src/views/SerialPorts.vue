<script setup lang="ts">
import { computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import InputNumber from '@/components/InputNumber.vue';
import Layout from '@/components/Layout.vue';
import RsSettings from '@/components/RsSettings.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
// info (incl. device signature) is loaded by the route's beforeEnter fetchInfo() and the
// global poller in App.vue, so this component only needs to read the shared singleton.
const { info } = useInfo();

// The Modbus role is fixed when the firmware is built: only a slave build has a Modbus identity
// of its own, and it is the only build whose /settings carries `mb_slave_id`/`mb_tcp_port`. The
// firmware announces the role nowhere else, so the presence of both keys IS the signal — with
// them absent the card is not rendered at all, and with it its inputs, their required/min/max
// constraints and its Save button.
const hasModbusSlave = computed(() =>
  data.value?.mb_slave_id !== undefined && data.value?.mb_tcp_port !== undefined
);

// Sends the two keys and nothing else, and only when the device actually has them: a build
// without a Modbus identity must not have `mb_slave_id: undefined` posted back to it, which
// would put the keys into a document that deliberately lacks them. The guard is what narrows
// the two optional fields to numbers, so the payload can never carry an undefined.
const saveModbus = () => {
  const settings = data.value;
  if (settings?.mb_slave_id === undefined || settings.mb_tcp_port === undefined) return;
  updateSettings({ mb_slave_id: settings.mb_slave_id, mb_tcp_port: settings.mb_tcp_port });
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <!-- The device's own Modbus identity: one address for both RS-485 ports and one TCP port
           for the Modbus TCP server. It belongs above the per-port cards, not inside them, and
           only a build that has such an identity renders it at all (see hasModbusSlave). -->
      <section v-if="hasModbusSlave" class="card">
        <form @submit.prevent="saveModbus">
          <div class="card-header">
            <div class="card-title-wrap">
              <div class="title">{{ t('modbus') }}</div>
              <div class="sub">{{ t('modbus_sub') }}</div>
            </div>
            <Button
              type="submit"
              :is-loading="isLoading && isChanged(['mb_slave_id', 'mb_tcp_port'])"
              :disabled="isLoading || !isChanged(['mb_slave_id', 'mb_tcp_port'])"
            >
              {{ t('save') }}
            </Button>
          </div>
          <div class="card-body">
            <div class="field">
              <label for="mb_slave_id">{{ t('mb_slave_id') }}</label>
              <InputNumber id="mb_slave_id" v-model="data.mb_slave_id" name="mb_slave_id" class="mono" min="1" max="247" required />
            </div>
            <div class="field">
              <label for="mb_tcp_port">{{ t('mb_tcp_port') }}</label>
              <InputNumber id="mb_tcp_port" v-model="data.mb_tcp_port" name="mb_tcp_port" class="mono" min="1" max="65535" required />
            </div>
          </div>
        </form>
      </section>

      <div class="grid-2">
        <RsSettings
          v-model:settings="data.rs485_1"
          field="rs485_1"
          :title="t('port_1')"
          :sub="t('port1_sub')"
        />

        <div class="stack">
          <RsSettings
            v-model:settings="data.rs485_2"
            field="rs485_2"
            :title="t('port_2')"
            :sub="t('port2_sub')"
            :signature="info?.signature"
          />

          <section class="card">
            <form @submit.prevent="updateSettings({ io_bus: data.io_bus })">
              <div class="card-header">
                <div class="card-title-wrap">
                  <div class="title">{{ t('io_bus') }}</div>
                  <div class="sub">{{ t('io_bus_sub') }}</div>
                </div>
                <Button
                  type="submit"
                  :is-loading="isLoading && isChanged(['io_bus'])"
                  :disabled="isLoading || !isChanged(['io_bus'])"
                >
                  {{ t('save') }}
                </Button>
              </div>
              <div class="card-body">
                <div class="field">
                  <label for="io_bus">{{ t('io_bus_enable') }}</label>
                  <div class="field-switch"><Switch id="io_bus" v-model="data.io_bus" /></div>
                </div>
              </div>
            </form>
          </section>
</div>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
.field-switch {
  justify-self: end;
}
</style>

<i18n>
{
  "en": {
    "title": "Serial ports",
    "crumbs": "RS-485 interfaces",
    "save": "Save",
    "modbus": "Modbus slave",
    "modbus_sub": "The device answers at this address on both RS-485 ports and serves Modbus TCP on this port.",
    "mb_slave_id": "Modbus address of the device",
    "mb_tcp_port": "Modbus TCP port",
    "io_bus_sub": "WB-MIO chip connected to RS-485 Port 2. Default address 247.",
    "io_bus_enable": "Enable I/O Bus",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "port1_sub": "Wired terminal · left",
    "port2_sub": "Wired terminal · right + I/O bus"
  },
  "ru": {
    "title": "Последовательные порты",
    "crumbs": "Интерфейсы RS-485",
    "save": "Сохранить",
    "modbus": "Устройство Modbus",
    "modbus_sub": "Устройство отвечает по этому адресу на обоих портах RS-485 и принимает Modbus TCP на этом порту.",
    "mb_slave_id": "Modbus-адрес устройства",
    "mb_tcp_port": "Порт Modbus TCP",
    "io_bus_sub": "Чип WB-MIO, подключённый к RS-485 Port 2. Адрес по умолчанию 247.",
    "io_bus_enable": "Включить I/O Bus",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "port1_sub": "Левый клеммник",
    "port2_sub": "Правый клеммник + I/O bus"
  },
  "kk": {
    "title": "Сериялық порттар",
    "crumbs": "RS-485 интерфейстері",
    "save": "Сақтау",
    "modbus": "Modbus құрылғысы",
    "modbus_sub": "Құрылғы екі RS-485 портында да осы мекенжай бойынша жауап береді және осы портта Modbus TCP қабылдайды.",
    "mb_slave_id": "Құрылғының Modbus мекенжайы",
    "mb_tcp_port": "Modbus TCP порты",
    "io_bus_sub": "RS-485 Port 2-ге қосылған WB-MIO чипі. Әдепкі адресі 247.",
    "io_bus_enable": "I/O Bus қосу",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "port1_sub": "Сымды клемма · сол",
    "port2_sub": "Сымды клемма · оң + I/O bus"
  },
  "it": {
    "title": "Porte seriali",
    "crumbs": "Interfacce RS-485",
    "save": "Salva",
    "modbus": "Dispositivo Modbus",
    "modbus_sub": "Il dispositivo risponde a questo indirizzo su entrambe le porte RS-485 e accetta Modbus TCP su questa porta.",
    "mb_slave_id": "Indirizzo Modbus del dispositivo",
    "mb_tcp_port": "Porta Modbus TCP",
    "io_bus_sub": "Chip WB-MIO collegato alla RS-485 Port 2. Indirizzo predefinito 247.",
    "io_bus_enable": "Abilita I/O Bus",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Porta 1",
    "port_2": "RS-485 · Porta 2",
    "port1_sub": "Morsettiera · sinistra",
    "port2_sub": "Morsettiera · destra + I/O bus"
  },
  "de": {
    "title": "Serielle Schnittstellen",
    "crumbs": "RS-485-Schnittstellen",
    "save": "Speichern",
    "modbus": "Modbus-Gerät",
    "modbus_sub": "Das Gerät antwortet unter dieser Adresse an beiden RS-485-Ports und nimmt Modbus TCP an diesem Port entgegen.",
    "mb_slave_id": "Modbus-Adresse des Geräts",
    "mb_tcp_port": "Modbus-TCP-Port",
    "io_bus_sub": "WB-MIO-Chip an RS-485 Port 2 angeschlossen. Standardadresse 247.",
    "io_bus_enable": "I/O Bus aktivieren",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "port1_sub": "Klemmenleiste · links",
    "port2_sub": "Klemmenleiste · rechts + I/O bus"
  }
}

</i18n>
