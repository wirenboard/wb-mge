<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import Heading from '@/components/Heading.vue';
import InfoRow from '@/components/InfoRow.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';
import RsStatus from '@/components/RsStatus.vue';

const { t } = useI18n();
const { info, startPolling, stopPolling } = useInfo();
const { data: settings, updateSettings } = useSettings();

onMounted(() => {
  startPolling();
});

onUnmounted(() => {
  stopPolling();
});

const getDisplayValue = (val: string | boolean | number) => {
  if (typeof val === 'boolean') {
    return val ? t('enabled') : t('disabled');
  } else {
    return val || '-';
  }
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" />

    <div class="main-body">
      <div class="dashboard">
        <section class="card">
          <div class="card-header"><div class="title">{{ t('ethernet') }}</div></div>
          <div class="card-body">
            <InfoRow :label="t('status')">{{ info!.ethernet.con_eth ? t('connected') : t('not_connected') }}</InfoRow>
            <InfoRow :label="t('ip')">{{ getDisplayValue(info!.ethernet.ip) }}</InfoRow>
            <InfoRow :label="t('mac')">{{ getDisplayValue(info!.ethernet.mac) }}</InfoRow>
          </div>
        </section>

        <section class="card">
          <div class="card-header"><div class="title">{{ t('wifi') }}</div></div>
          <div class="card-body">
            <InfoRow :label="t('status')">{{ getDisplayValue(info!.wifi.enabled) }}</InfoRow>
            <InfoRow :label="t('wifi_mode')">{{ t(info!.wifi.mode) }}</InfoRow>

            <template v-if="info!.wifi.mode === 'ap'">
              <InfoRow :label="t('connections_count')">{{ info!.wifi.con_ap }}</InfoRow>
              <InfoRow :label="t('ip')">{{ info!.wifi.ap_ip }}</InfoRow>
              <InfoRow :label="t('mac')">{{ info!.wifi.ap_mac }}</InfoRow>
            </template>

            <template v-else-if="info!.wifi.mode === 'sta'">
              <InfoRow :label="t('connection')">{{ info!.wifi.con_sta ? t('connected') : t('not_connected') }}</InfoRow>

              <template v-if="info!.wifi.con_sta">
                <InfoRow :label="t('ssid')">{{ info!.wifi.con_sta_ssid }}</InfoRow>
              </template>

              <InfoRow :label="t('ip')">{{ info!.wifi.sta_ip }}</InfoRow>
              <InfoRow :label="t('mac')">{{ info!.wifi.sta_mac }}</InfoRow>

              <template v-if="info!.wifi.enabled && info!.wifi.con_sta">
                <InfoRow :label="t('rssi')">{{ info?.wifi.sta_rssi }} {{ t('dbm') }}</InfoRow>
              </template>
            </template>
          </div>
        </section>

        <section class="card">
          <div class="card-header"><div class="title">{{ t('gateway') }}</div></div>
          <div class="card-body">
            <InfoRow :label="t('power_vout')">
              <Switch
                id="power_vout"
                v-model="settings!.vout"
                @change="() => updateSettings({ vout: settings!.vout })"
              />
            </InfoRow>
            <InfoRow :label="t('power')">{{ Number(info?.system_voltage.toFixed(1)) }} {{ t('v') }}</InfoRow>
            <RsStatus title="RS-485 1" :info="info!.rs485_1" :settings="settings!.rs485_1" />
          </div>
        </section>

        <section v-if="info!.knx" class="card">
          <div class="card-header"><div class="title">{{ t('knx') }}</div></div>
          <div class="card-body">
            <InfoRow :label="t('status')">{{ info!.knx.running ? t('running') : t('stopped') }}</InfoRow>
            <InfoRow :label="t('knx_bus')">{{ info!.knx.bus_alive ? t('alive') : t('not_connected') }}</InfoRow>
            <InfoRow :label="t('knx_tcp_port')">{{ info!.knx.tcp_port }}</InfoRow>
            <InfoRow :label="t('knx_clients')">{{ info!.knx.clients_count }} ({{ info!.knx.secure_count }} {{ t('knx_secure') }})</InfoRow>
            <InfoRow :label="t('knx_to_bus')">{{ info!.knx.rx_count }}</InfoRow>
            <InfoRow :label="t('knx_from_bus')">{{ info!.knx.tx_count }}</InfoRow>
          </div>
        </section>
      </div>
    </div>
  </Layout>
</template>

<style>
.dashboard {
  columns: 2;
  column-gap: 20px;

  @media (max-width: 1024px) {
    columns: 1;
  }
}

/* Cards in dashboard must not break across columns */
.dashboard .card {
  break-inside: avoid;
  page-break-inside: avoid;
  margin-bottom: 20px;
}
</style>

<i18n>
{
  "en": {
    "title": "Dashboard",

    "status": "Status",
    "connection": "Connection",
    "ip": "IP address",
    "mac": "MAC address",
    "enabled": "Enabled",
    "connected": "Connected",
    "not_connected": "Not connected",
    "disabled": "Disabled",

    "ethernet": "Ethernet",

    "wifi": "Wi-Fi",
    "wifi_mode": "Mode",
    "client": "Client",
    "access_point": "Access Point",
    "connections_count": "Number of connections",
    "ssid": "SSID",
    "rssi": "RSSI",
    "dbm": "dBm",

    "gateway": "Gateway",
    "power_vout": "Power Vout",
    "power": "Power",
    "v": "V",

    "knx": "KNX",
    "running": "Running",
    "stopped": "Stopped",
    "knx_bus": "KNX bus",
    "alive": "Alive",
    "knx_tcp_port": "TCP port",
    "knx_clients": "Connected clients",
    "knx_secure": "secure",
    "knx_to_bus": "Telegrams to bus",
    "knx_from_bus": "Telegrams from bus"
  },
  "ru": {
    "title": "Обзор",

    "status": "Состояние",
    "connection": "Подключение",
    "ip": "IP-адрес",
    "mac": "MAC-адрес",
    "enabled": "Включено",
    "connected": "Подключено",
    "not_connected": "Не подключено",
    "disabled": "Отключено",

    "ethernet": "Ethernet",

    "wifi": "Wi-Fi",
    "client": "Клиент",
    "access_point": "Точка доступа",
    "wifi_mode": "Роль",
    "connections_count": "Количество подключений",
    "ssid": "SSID",
    "rssi": "RSSI",
    "dbm": "дБ",

    "gateway": "Шлюз",
    "power_vout": "Питание Vout",
    "power": "Напряжение питания",
    "v": "В"
  },
  "kk": {
    "title": "Шолу",

    "status": "Күйі",
    "connection": "Қосылым",
    "ip": "IP мекенжайы",
    "mac": "MAC мекенжайы",
    "enabled": "Қосулы",
    "connected": "Қосылған",
    "not_connected": "Қосылмаған",
    "disabled": "Өшірілген",

    "ethernet": "Ethernet",

    "wifi": "Wi-Fi",
    "wifi_mode": "Режим",
    "client": "Клиент",
    "access_point": "Қатынас нүктесі",
    "connections_count": "Қосылымдар саны",
    "ssid": "SSID",
    "rssi": "RSSI",
    "dbm": "dBm",

    "gateway": "Gateway",
    "power_vout": "Vout кернеуі",
    "power": "Қорек кернеуі",
    "v": "В"
  },
  "it": {
    "title": "Dashboard",

    "status": "Stato",
    "connection": "Connessione",
    "ip": "Indirizzo IP",
    "mac": "Indirizzo MAC",
    "enabled": "Abilitato",
    "connected": "Connesso",
    "not_connected": "Non connesso",
    "disabled": "Disabilitato",

    "ethernet": "Ethernet",

    "wifi": "Wi-Fi",
    "wifi_mode": "Modalità",
    "client": "Client",
    "access_point": "Access Point",
    "connections_count": "Numero di connessioni",
    "ssid": "SSID",
    "rssi": "RSSI",
    "dbm": "dBm",

    "gateway": "Gateway",
    "power_vout": "Tensione Vout",
    "power": "Tensione di alimentazione",
    "v": "V"
  },
  "de": {
    "title": "Übersicht",

    "status": "Status",
    "connection": "Verbindung",
    "ip": "IP-Adresse",
    "mac": "MAC-Adresse",
    "enabled": "Aktiviert",
    "connected": "Verbunden",
    "not_connected": "Nicht verbunden",
    "disabled": "Deaktiviert",

    "ethernet": "Ethernet",

    "wifi": "Wi-Fi",
    "wifi_mode": "Modus",
    "client": "Client",
    "access_point": "Access Point",
    "connections_count": "Anzahl der Verbindungen",
    "ssid": "SSID",
    "rssi": "RSSI",
    "dbm": "dBm",

    "gateway": "Gateway",
    "power_vout": "Vout-Versorgung",
    "power": "Versorgungsspannung",
    "v": "V"
  }
}
</i18n>
