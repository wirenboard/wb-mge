<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';
import RsStatus from '@/components/RsStatus.vue';

const { t } = useI18n();
const { info } = useInfo();
const { data: settings, updateSettings } = useSettings();

const getDisplayValue = (val: any) => {
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

        <div>{{ t('connection') }}</div>
        <div>{{ getDisplayValue(info!.ethernet.con_eth) }}</div>

        <div>{{ t('ip') }}</div>
        <div>{{ getDisplayValue(info!.ethernet.eth_ip) }}</div>

        <div>{{ t('mac') }}</div>
        <div>{{ getDisplayValue(info!.ethernet.eth_mac) }}</div>
      </fieldset>

      <fieldset class="dashboard-container">
        <legend>{{ t('wifi') }}</legend>

        <div>{{ t('connection') }}</div>
        <div>{{ getDisplayValue(info!.wifi.con_sta) }}</div>

        <template v-if="settings!.wifi.mode === 'apsta'">
          <div>{{ t('ap_ip') }}</div>
          <div>{{ settings!.wifi.ap_ip_static }}</div>

          <div>{{ t('ap_mac') }}</div>
          <div>{{ info!.wifi.ap_mac }}</div>

          <div>{{ t('station_ip') }}</div>
          <div>{{ info!.wifi.sta_ip }}</div>

          <div>{{ t('station_mac') }}</div>
          <div>{{ info!.wifi.sta_mac }}</div>
        </template>
        <template v-else>
          <div>{{ t('ip') }}</div>
          <div>{{ getDisplayValue(settings!.wifi.mode === 'sta' ? info!.wifi.sta_ip : settings!.wifi.ap_ip_static) }}</div>

          <div>{{ t('mac') }}</div>
          <div>{{ getDisplayValue(settings!.wifi.mode === 'sta' ? info!.wifi.sta_mac : info!.wifi.ap_mac) }}</div>
        </template>

        <div>{{ t('wifi_mode') }}</div>
        <div>{{ t(settings!.wifi.mode) }}</div>

        <template v-if="['sta', 'apsta'].includes(settings!.wifi.mode)">
          <div>{{ t('rssi') }}</div>
          <div>{{ info?.wifi.sta_rssi }}</div>
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

    "connection": "Connection",
    "ip": "IP address",
    "mac": "MAC address",
    "enabled": "Enabled",
    "disabled": "Disabled",

    "ethernet": "Ethernet",

    "wifi": "Wi-Fi",
    "wifi_mode": "Mode",
    "ap_ip": "Access Point IP address",
    "ap_mac": "Access Point MAC address",
    "station_ip": "Station IP address",
    "station_mac": "Station MAC address",
    "rssi": "Station RSSI",

    "gateway": "Gateway",
    "power_vout": "Power Vout"
  },
  "ru": {
    "title": "Обзор",

    "connection": "Состояние",
    "ip": "IP-адрес",
    "mac": "MAC-адрес",
    "enabled": "Подключено",
    "disabled": "Отключено",

    "ethernet": "Ethernet",

    "wifi": "Wi-Fi",
    "wifi_mode": "Роль",
    "ap_ip": "IP-адрес точки доступа",
    "ap_mac": "MAC-адрес точки доступа",
    "station_ip": "IP-адрес станции",
    "station_mac": "MAC-адрес станции",
    "rssi": "RSSI станции",

    "gateway": "Шлюз",
    "power_vout": "Питание Vout"
  }
}
</i18n>
