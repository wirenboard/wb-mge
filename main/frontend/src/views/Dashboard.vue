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

    <div class="main-body">
    <div class="grid-2">
      <div class="stack">
        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('ethernet') }}</div>
          </div>
          <div class="card-body">
            <div class="kv">
              <div class="k">{{ t('status') }}</div>
              <div class="v">{{ info!.ethernet.con_eth ? t('connected') : t('not_connected') }}</div>
            </div>
            <div class="kv">
              <div class="k">{{ t('ip') }}</div>
              <div class="v mono">{{ getDisplayValue(info!.ethernet.ip) }}</div>
            </div>
            <div class="kv">
              <div class="k">{{ t('mac') }}</div>
              <div class="v mono">{{ getDisplayValue(info!.ethernet.mac) }}</div>
            </div>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('wifi') }}</div>
          </div>
          <div class="card-body">
            <div class="kv">
              <div class="k">{{ t('status') }}</div>
              <div class="v">{{ getDisplayValue(info!.wifi.enabled) }}</div>
            </div>
            <div class="kv">
              <div class="k">{{ t('wifi_mode') }}</div>
              <div class="v">{{ t(info!.wifi.mode) }}</div>
            </div>

            <template v-if="info!.wifi.mode === 'ap'">
              <div class="kv">
                <div class="k">{{ t('connections_count') }}</div>
                <div class="v">{{ info!.wifi.con_ap }}</div>
              </div>
              <div class="kv">
                <div class="k">{{ t('ip') }}</div>
                <div class="v mono">{{ info!.wifi.ap_ip }}</div>
              </div>
              <div class="kv">
                <div class="k">{{ t('mac') }}</div>
                <div class="v mono">{{ info!.wifi.ap_mac }}</div>
              </div>
            </template>

            <template v-else-if="info!.wifi.mode === 'sta'">
              <div class="kv">
                <div class="k">{{ t('connection') }}</div>
                <div class="v">{{ info!.wifi.con_sta ? t('connected') : t('not_connected') }}</div>
              </div>
              <template v-if="info!.wifi.con_sta">
                <div class="kv">
                  <div class="k">{{ t('ssid') }}</div>
                  <div class="v mono">{{ info!.wifi.con_sta_ssid }}</div>
                </div>
              </template>
              <div class="kv">
                <div class="k">{{ t('ip') }}</div>
                <div class="v mono">{{ info!.wifi.sta_ip }}</div>
              </div>
              <div class="kv">
                <div class="k">{{ t('mac') }}</div>
                <div class="v mono">{{ info!.wifi.sta_mac }}</div>
              </div>
              <template v-if="info!.wifi.enabled && info!.wifi.con_sta">
                <div class="kv">
                  <div class="k">{{ t('rssi') }}</div>
                  <div class="v">{{ info?.wifi.sta_rssi }} {{ t('dbm') }}</div>
                </div>
              </template>
            </template>
          </div>
        </section>
      </div>

      <div class="stack">
        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('gateway') }}</div>
            <div style="display:flex;align-items:center;gap:10px">
              <span style="font-size:12px;color:var(--text-secondary)">V<sub>out</sub></span>
              <Switch
                id="power_vout"
                v-model="settings!.vout"
                @change="() => updateSettings({ vout: settings!.vout })"
              />
            </div>
          </div>
          <div class="card-body">
            <div class="kv">
              <div class="k">{{ t('power') }}</div>
              <div class="v mono">{{ Number(info?.system_voltage.toFixed(1)) }} {{ t('v') }}</div>
            </div>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">RS-485 · Port 1</div>
          </div>
          <div class="card-body">
            <RsStatus title="RS-485 1" :info="info!.rs485_1" :settings="settings!.rs485_1" />
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">RS-485 · Port 2</div>
          </div>
          <div class="card-body">
            <RsStatus title="RS-485 2" :info="info!.rs485_2" :settings="settings!.rs485_2" />
          </div>
        </section>
      </div>
    </div>
    </div>
  </Layout>
</template>

<style>
.mono { font-family: var(--font-mono); }
.muted { color: var(--text-muted); }
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
