<script setup lang="ts">
import { computed, onMounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { useChannelRelease } from '@/common/channelRelease';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import { useUptime } from '@/common/uptime';
import { useRouter } from 'vue-router';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import RsStatus from '@/components/RsStatus.vue';
import InfoRow from '@/components/InfoRow.vue';
import GearIcon from '@/assets/gearIcon.svg?component';

const { t } = useI18n();
const { info } = useInfo();
const { data: settings } = useSettings();
const { uptime } = useUptime();
const router = useRouter();

// The offer comes from the shared composable, so this page and System always name the same
// version — and the manifest is downloaded once, no matter which page is opened first.
const { phase: updatePhase, release, resolvedChannel, check: checkUpdates } = useChannelRelease();

onMounted(() => {
  checkUpdates();
});

const hasUpdate = computed(() => updatePhase.value === 'available');

const firmwareNote = computed(() => {
  const found = release.value;
  switch (updatePhase.value) {
    case 'checking':
      return t('firmware_checking');
    case 'available':
      return found?.ok ? t('firmware_in_channel', { channel: resolvedChannel.value, v: found.version }) : '';
    // Naming the channel version here too would read as "1.1.0 (in stable: 1.1.0)" — the same
    // number twice, with nothing telling the user it means there is nothing to install.
    case 'up_to_date':
      return t('firmware_up_to_date');
    case 'unavailable':
      return found && !found.ok && found.reason === 'no-signature'
        ? t('firmware_channels_unavailable')
        : t('firmware_check_failed');
    default:
      return '';
  }
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
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <div class="grid-2">
      <div class="stack">
        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">{{ t('ethernet') }}</div>
              <button class="card-edit-btn" :title="t('edit_settings')" :aria-label="t('edit_settings')" @click="router.push('/network')">
                <GearIcon />
              </button>
            </div>
            <span v-if="info!.ethernet.con_eth" class="pill ok"><span class="dot" />{{ t('connected') }}</span>
            <span v-else class="pill muted">{{ t('not_connected') }}</span>
          </div>
          <div class="card-body">
            <InfoRow :label="t('ip')"><span class="mono">{{ getDisplayValue(info!.ethernet.ip) }}</span></InfoRow>
            <InfoRow :label="t('mac')"><span class="mono">{{ getDisplayValue(info!.ethernet.mac) }}</span></InfoRow>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">{{ t('wifi') }}</div>
              <button class="card-edit-btn" :title="t('edit_settings')" :aria-label="t('edit_settings')" @click="router.push('/network')">
                <GearIcon />
              </button>
            </div>
            <span v-if="info!.wifi.enabled" class="pill ok"><span class="dot" />{{ t('enabled') }}</span>
            <span v-else class="pill muted">{{ t('disabled') }}</span>
          </div>
          <div class="card-body">
            <InfoRow :label="t('status')">{{ getDisplayValue(info!.wifi.enabled) }}</InfoRow>
            <InfoRow :label="t('wifi_mode')">{{ t(info!.wifi.mode) }}</InfoRow>

            <template v-if="info!.wifi.mode === 'ap'">
              <InfoRow :label="t('connections_count')">{{ info!.wifi.con_ap }}</InfoRow>
              <InfoRow :label="t('ip')"><span class="mono">{{ info!.wifi.ap_ip }}</span></InfoRow>
              <InfoRow :label="t('mac')"><span class="mono">{{ info!.wifi.ap_mac }}</span></InfoRow>
            </template>

            <template v-else-if="info!.wifi.mode === 'sta'">
              <InfoRow :label="t('connection')">{{ info!.wifi.con_sta ? t('connected') : t('not_connected') }}</InfoRow>
              <template v-if="info!.wifi.con_sta">
                <InfoRow :label="t('ssid')"><span class="mono">{{ info!.wifi.con_sta_ssid }}</span></InfoRow>
              </template>
              <InfoRow :label="t('ip')"><span class="mono">{{ info!.wifi.sta_ip }}</span></InfoRow>
              <InfoRow :label="t('mac')"><span class="mono">{{ info!.wifi.sta_mac }}</span></InfoRow>
              <template v-if="info!.wifi.enabled && info!.wifi.con_sta">
                <InfoRow :label="t('rssi')">{{ info?.wifi.sta_rssi }} {{ t('dbm') }}</InfoRow>
              </template>
            </template>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('gateway') }}</div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('power')"><span class="mono">{{ info?.system_voltage?.toFixed(1) }} {{ t('v') }}</span></InfoRow>
            <InfoRow :label="t('uptime')">
              <span class="muted uptime-value">
                <template v-if="uptime">
                  <template v-if="uptime.days">
                    <span>{{ t('uptime_days', { n: uptime.days }) }}</span>
                  </template>
                  <template v-if="uptime.hours">
                    <span>{{ t('uptime_hours', { n: uptime.hours }) }}</span>
                  </template>
                  <span>{{ t('uptime_minutes', { n: uptime.minutes }) }}</span>
                </template>
                <template v-else>
                  <span>—</span>
                </template>
              </span>
            </InfoRow>
            <InfoRow :label="t('firmware_version')">
              <span class="firmware-row">
                <span class="mono">{{ info?.firmware }}<template v-if="firmwareNote"><span class="muted firmware-note"> ({{ firmwareNote }})</span></template></span>
                <Button v-if="hasUpdate" type="button" variant="primary" @click="router.push('/system')">{{ t('firmware_update_btn') }}</Button>
              </span>
            </InfoRow>
          </div>
        </section>
      </div>

      <div class="stack">
        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">{{ t('port_1') }}</div>
              <button class="card-edit-btn" :title="t('edit_settings')" :aria-label="t('edit_settings')" @click="router.push('/settings')">
                <GearIcon />
              </button>
            </div>
            <span v-if="info!.rs485_1.is_busy" class="pill ok"><span class="dot" />{{ t('active') }}</span>
            <span v-else class="pill muted">{{ t('inactive') }}</span>
          </div>
          <div class="card-body">
            <RsStatus :title="t('rs_status_1')" :info="info!.rs485_1" :settings="settings!.rs485_1" />
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">{{ t('port_2') }}</div>
              <button class="card-edit-btn" :title="t('edit_settings')" :aria-label="t('edit_settings')" @click="router.push('/settings')">
                <GearIcon />
              </button>
            </div>
            <span v-if="info!.rs485_2.is_busy" class="pill ok"><span class="dot" />{{ t('active') }}</span>
            <span v-else class="pill muted">{{ t('inactive') }}</span>
          </div>
          <div class="card-body">
            <RsStatus :title="t('rs_status_2')" :info="info!.rs485_2" :settings="settings!.rs485_2" />
          </div>
        </section>
      </div>
      </div>
    </div>
  </Layout>
</template>

<style>
.uptime-value {
  display: flex;
  gap: 4px;
}
.firmware-row {
  display: flex;
  align-items: center;
  gap: 8px;
  justify-content: flex-end;
}
.firmware-update-link {
  font-size: 11.5px;
  color: var(--color-primary, #2563eb);
  text-decoration: none;
  white-space: nowrap;
}
.firmware-update-link:hover {
  text-decoration: underline;
}
.firmware-note {
  font-size: 11.5px;
}
</style>

<i18n>
{
  "en": {
    "title": "Dashboard",
    "crumbs": "Gateway overview",

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
    "gateway_sub": "Power & auxiliary output",
    "power": "Supply voltage",
    "v": "V",
    "active": "Active",
    "inactive": "Inactive",
    "edit_settings": "Settings",
    "uptime": "Uptime",
    "uptime_days": "- | {n} day | {n} days | {n} days",
    "uptime_hours": "less than an hour | {n} h | {n} h | {n} h",
    "uptime_minutes": "{n} min",
    "firmware_version": "Firmware",
    "firmware_checking": "checking for updates…",
    "firmware_in_channel": "in {channel}: {v}",
    "firmware_up_to_date": "up to date",
    "firmware_check_failed": "update check unavailable",
    "firmware_channels_unavailable": "no update channels published for this board yet",
    "firmware_update_btn": "Update",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "rs_status_1": "RS-485 1",
    "rs_status_2": "RS-485 2"
  },
  "ru": {
    "title": "Обзор",
    "crumbs": "Обзор шлюза",

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
    "gateway_sub": "Питание и вспомогательный выход",
    "power": "Напряжение питания",
    "v": "В",
    "active": "Активен",
    "inactive": "Неактивен",
    "edit_settings": "Настройки",
    "uptime": "Время работы",
    "uptime_days": "- | {n} день | {n} дня | {n} дней",
    "uptime_hours": "- | {n} ч | {n} ч | {n} ч",
    "uptime_minutes": "{n} мин",
    "firmware_version": "Прошивка",
    "firmware_checking": "проверка обновлений…",
    "firmware_in_channel": "в канале {channel}: {v}",
    "firmware_up_to_date": "актуальная",
    "firmware_check_failed": "проверка обновлений недоступна",
    "firmware_channels_unavailable": "для этой платы каналы обновлений пока не опубликованы",
    "firmware_update_btn": "Обновить",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "rs_status_1": "RS-485 1",
    "rs_status_2": "RS-485 2"
  },
  "kk": {
    "title": "Шолу",
    "crumbs": "Шлюзге шолу",

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
    "gateway_sub": "Қуат және көмекші шығыс",
    "power": "Қорек кернеуі",
    "v": "В",
    "active": "Белсенді",
    "inactive": "Белсенді емес",
    "edit_settings": "Баптаулар",
    "uptime": "Жұмыс уақыты",
    "uptime_days": "- | {n} күн | {n} күн | {n} күн",
    "uptime_hours": "- | {n} сағ | {n} сағ | {n} сағ",
    "uptime_minutes": "{n} мин",
    "firmware_version": "Бағдарлама",
    "firmware_checking": "жаңартулар тексерілуде…",
    "firmware_in_channel": "{channel} арнасында: {v}",
    "firmware_up_to_date": "өзекті",
    "firmware_check_failed": "жаңарту тексерісі қолжетімсіз",
    "firmware_channels_unavailable": "бұл тақта үшін жаңарту арналары әзірге жарияланбаған",
    "firmware_update_btn": "Жаңарту",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "rs_status_1": "RS-485 1",
    "rs_status_2": "RS-485 2"
  },
  "it": {
    "title": "Dashboard",
    "crumbs": "Panoramica gateway",

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
    "gateway_sub": "Alimentazione e uscita ausiliaria",
    "power": "Tensione di alimentazione",
    "v": "V",
    "active": "Attivo",
    "inactive": "Inattivo",
    "edit_settings": "Impostazioni",
    "uptime": "Tempo di attività",
    "uptime_days": "- | {n} giorno | {n} giorni | {n} giorni",
    "uptime_hours": "- | {n} h | {n} h | {n} h",
    "uptime_minutes": "{n} min",
    "firmware_version": "Firmware",
    "firmware_checking": "controllo aggiornamenti…",
    "firmware_in_channel": "nel canale {channel}: {v}",
    "firmware_up_to_date": "aggiornato",
    "firmware_check_failed": "controllo aggiornamenti non disponibile",
    "firmware_channels_unavailable": "per questa scheda i canali di aggiornamento non sono ancora pubblicati",
    "firmware_update_btn": "Aggiorna",
    "port_1": "RS-485 · Porta 1",
    "port_2": "RS-485 · Porta 2",
    "rs_status_1": "RS-485 1",
    "rs_status_2": "RS-485 2"
  },
  "de": {
    "title": "Übersicht",
    "crumbs": "Gateway-Übersicht",

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
    "gateway_sub": "Stromversorgung und Hilfsausgang",
    "power": "Versorgungsspannung",
    "v": "V",
    "active": "Aktiv",
    "inactive": "Inaktiv",
    "edit_settings": "Einstellungen",
    "uptime": "Betriebszeit",
    "uptime_days": "- | {n} Tag | {n} Tage | {n} Tage",
    "uptime_hours": "- | {n} h | {n} h | {n} h",
    "uptime_minutes": "{n} min",
    "firmware_version": "Firmware",
    "firmware_checking": "Update-Prüfung läuft…",
    "firmware_in_channel": "im Kanal {channel}: {v}",
    "firmware_up_to_date": "aktuell",
    "firmware_check_failed": "Update-Prüfung nicht verfügbar",
    "firmware_channels_unavailable": "für diese Platine sind noch keine Update-Kanäle veröffentlicht",
    "firmware_update_btn": "Aktualisieren",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "rs_status_1": "RS-485 1",
    "rs_status_2": "RS-485 2"
  }
}
</i18n>
