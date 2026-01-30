<script setup lang="ts">
import { onMounted, onUnmounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import Heading from '@/components/Heading.vue';
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

    <div class="dashboard">
      <fieldset class="dashboard-container">
        <legend>{{ t('ethernet') }}</legend>

        <div>{{ t('status') }}</div>
        <div>{{ info!.ethernet.con_eth ? t('connected') : t('not_connected') }}</div>

        <div>{{ t('ip') }}</div>
        <div>{{ getDisplayValue(info!.ethernet.ip) }}</div>

        <div>{{ t('mac') }}</div>
        <div>{{ getDisplayValue(info!.ethernet.mac) }}</div>
      </fieldset>

      <fieldset class="dashboard-container">
        <legend>{{ t('wifi') }}</legend>

        <div>{{ t('status') }}</div>
        <div>{{ getDisplayValue(info!.wifi.enabled) }}</div>

        <div>{{ t('wifi_mode') }}</div>
        <div>{{ t(info!.wifi.mode) }}</div>

        <template v-if="info!.wifi.mode === 'ap'">
          <div>{{ t('connections_count') }}</div>
          <div>{{ info!.wifi.con_ap }}</div>

          <div>{{ t('ip') }}</div>
          <div>{{ info!.wifi.ap_ip }}</div>

          <div>{{ t('mac') }}</div>
          <div>{{ info!.wifi.ap_mac }}</div>
        </template>

        <template v-else-if="info!.wifi.mode === 'sta'">
          <div>{{ t('connection') }}</div>
          <div>{{ info!.wifi.con_sta ? t('connected') : t('not_connected') }}</div>

          <template v-if="info!.wifi.con_sta">
            <div>{{ t('ssid') }}</div>
            <div>{{ info!.wifi.con_sta_ssid }}</div>
          </template>

          <div>{{ t('ip') }}</div>
          <div>{{ info!.wifi.sta_ip }}</div>

          <div>{{ t('mac') }}</div>
          <div>{{ info!.wifi.sta_mac }}</div>

          <template v-if="info!.wifi.enabled && info!.wifi.con_sta">
            <div>{{ t('rssi') }}</div>
            <div>{{ info?.wifi.sta_rssi }} {{ t('dbm') }}</div>
          </template>
        </template>
      </fieldset>

      <fieldset class="dashboard-container">
        <legend>{{ t('gateway') }}</legend>

        <div>{{ t('power_vout') }}</div>
        <div>
          <Switch
            id="power_vout"
            v-model="settings!.vout"
            @change="() => updateSettings({ vout: settings!.vout })"
          />
        </div>
        <div>{{ t('power') }}</div>
        <div>{{ Number(info?.system_voltage.toFixed(1)) }} {{ t('v') }}</div>

        <RsStatus title="RS-485 1" :info="info!.rs485_1" :settings="settings!.rs485_1" />

        <RsStatus title="RS-485 2" :info="info!.rs485_2" :settings="settings!.rs485_2" />
      </fieldset>
    </div>
  </Layout>
</template>

<style>
.dashboard {
  columns: 2;
  column-gap: 12px;

  @media (max-width: 1320px) {
    columns: 2;
  }

  @media (max-width: 1024px) {
    columns: 1;
    max-width: 470px;
  }

  @media (max-width: 500px) {
    width: 100%;
  }
}

.dashboard-container {
  display: grid;
  gap: 6px 24px;
  grid-template-columns: 1fr auto;
  align-items: center;
  justify-items: end;
  page-break-inside: avoid;
  break-inside: avoid;
}

.dashboard-container div {
  height: 33px;
}

.dashboard-container div:nth-child(even) {
  justify-self: start;
}

.dashboard-container div:nth-child(odd) {
  justify-self: end;
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
    "rssi": "RSSI",
    "dbm": "dBm",

    "gateway": "Gateway",
    "power_vout": "Power Vout",
    "power": "Power",
    "v": "V"
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
    "rssi": "RSSI",
    "dbm": "dBm",

    "gateway": "Gateway",
    "power_vout": "Vout-Versorgung",
    "power": "Versorgungsspannung",
    "v": "V"
  }
}
</i18n>
