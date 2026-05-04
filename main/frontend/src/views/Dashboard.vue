<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import { useUptime } from '@/common/uptime';
import { firmwareLatest, firmwareLatestVersion } from '@/common/links';
import { useRouter } from 'vue-router';
import SettingsIcon from '@/assets/settings.svg?component';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import RsStatus from '@/components/RsStatus.vue';

const { t } = useI18n();
const { info, startPolling, stopPolling } = useInfo();
const { data: settings, updateSettings } = useSettings();
const { uptime } = useUptime();
const router = useRouter();

const latestVersion = ref<string | null>(null);
const latestVersionError = ref(false);

onMounted(async () => {
  try {
    const res = await fetch(firmwareLatestVersion);
    if (!res.ok) throw new Error();
    latestVersion.value = (await res.text()).trim();
  } catch {
    latestVersionError.value = true;
  }
});

const hasUpdate = computed(() =>
  latestVersion.value && info.value?.firmware && latestVersion.value !== info.value.firmware
);

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
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <div class="grid-2">
      <div class="stack">
        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">{{ t('ethernet') }}</div>
              <button class="card-edit-btn" @click="router.push('/network')" :title="t('edit_settings')">
                <SettingsIcon />
              </button>
            </div>
            <span class="pill ok" v-if="info!.ethernet.con_eth"><span class="dot" />{{ t('connected') }}</span>
            <span class="pill muted" v-else>{{ t('not_connected') }}</span>
          </div>
          <div class="card-body">
            <div class="kv-row">
              <div class="kv-row-key">{{ t('ip') }}</div>
              <div class="kv-row-value mono">{{ getDisplayValue(info!.ethernet.ip) }}</div>
            </div>
            <div class="kv-row">
              <div class="kv-row-key">{{ t('mac') }}</div>
              <div class="kv-row-value mono">{{ getDisplayValue(info!.ethernet.mac) }}</div>
            </div>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">{{ t('wifi') }}</div>
              <button class="card-edit-btn" @click="router.push('/network')" :title="t('edit_settings')">
                <SettingsIcon />
              </button>
            </div>
            <span class="pill ok" v-if="info!.wifi.enabled"><span class="dot" />{{ t('enabled') }}</span>
            <span class="pill muted" v-else>{{ t('disabled') }}</span>
          </div>
          <div class="card-body">
            <div class="kv-row">
              <div class="kv-row-key">{{ t('status') }}</div>
              <div class="kv-row-value">{{ getDisplayValue(info!.wifi.enabled) }}</div>
            </div>
            <div class="kv-row">
              <div class="kv-row-key">{{ t('wifi_mode') }}</div>
              <div class="kv-row-value">{{ t(info!.wifi.mode) }}</div>
            </div>

            <template v-if="info!.wifi.mode === 'ap'">
              <div class="kv-row">
                <div class="kv-row-key">{{ t('connections_count') }}</div>
                <div class="kv-row-value">{{ info!.wifi.con_ap }}</div>
              </div>
              <div class="kv-row">
                <div class="kv-row-key">{{ t('ip') }}</div>
                <div class="kv-row-value mono">{{ info!.wifi.ap_ip }}</div>
              </div>
              <div class="kv-row">
                <div class="kv-row-key">{{ t('mac') }}</div>
                <div class="kv-row-value mono">{{ info!.wifi.ap_mac }}</div>
              </div>
            </template>

            <template v-else-if="info!.wifi.mode === 'sta'">
              <div class="kv-row">
                <div class="kv-row-key">{{ t('connection') }}</div>
                <div class="kv-row-value">{{ info!.wifi.con_sta ? t('connected') : t('not_connected') }}</div>
              </div>
              <template v-if="info!.wifi.con_sta">
                <div class="kv-row">
                  <div class="kv-row-key">{{ t('ssid') }}</div>
                  <div class="kv-row-value mono">{{ info!.wifi.con_sta_ssid }}</div>
                </div>
              </template>
              <div class="kv-row">
                <div class="kv-row-key">{{ t('ip') }}</div>
                <div class="kv-row-value mono">{{ info!.wifi.sta_ip }}</div>
              </div>
              <div class="kv-row">
                <div class="kv-row-key">{{ t('mac') }}</div>
                <div class="kv-row-value mono">{{ info!.wifi.sta_mac }}</div>
              </div>
              <template v-if="info!.wifi.enabled && info!.wifi.con_sta">
                <div class="kv-row">
                  <div class="kv-row-key">{{ t('rssi') }}</div>
                  <div class="kv-row-value">{{ info?.wifi.sta_rssi }} {{ t('dbm') }}</div>
                </div>
              </template>
            </template>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('gateway') }}</div>
          </div>
          <div class="card-body">
            <div class="kv-row">
              <div class="kv-row-key">{{ t('power') }}</div>
              <div class="kv-row-value mono">{{ Number(info?.system_voltage.toFixed(1)) }} {{ t('v') }}</div>
            </div>
            <div class="kv-row">
              <div class="kv-row-key">{{ t('uptime') }}</div>
              <div class="kv-row-value muted uptime-value">
                <template v-if="uptime">
                  <template v-if="uptime.days">
                    <span>{{ t('uptime_days', { n: uptime.days }) }}</span>
                  </template>
                  <template v-if="uptime.hours">
                    <span>{{ t('uptime_hours', { n: uptime.hours }) }}</span>
                  </template>
                  <span v-if="uptime.minutes > 0 || (!uptime.days && !uptime.hours && !uptime.seconds)">{{ t('uptime_minutes', { n: uptime.minutes }) }}</span>
                  <span v-if="uptime.seconds > 0 || (!uptime.days && !uptime.hours && !uptime.minutes)">{{ t('uptime_seconds', { n: uptime.seconds }) }}</span>
                </template>
                <template v-else>
                  <span>—</span>
                </template>
              </div>
            </div>
            <div class="kv-row">
              <div class="kv-row-key">{{ t('firmware_version') }}</div>
              <div class="kv-row-value firmware-row">
                <span class="mono">{{ info?.firmware }}<template v-if="latestVersion && !hasUpdate"> <span class="muted firmware-hint">({{ t('firmware_latest') }})</span></template><template v-else-if="hasUpdate"> <span class="muted firmware-hint">({{ t('firmware_latest_label') }} <span class="mono">{{ latestVersion }}</span>)</span></template></span>
                <Button v-if="hasUpdate" type="button" variant="primary" @click="router.push('/system')">{{ t('firmware_update_btn') }}</Button>
              </div>
            </div>
          </div>
        </section>
      </div>

      <div class="stack">
        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">RS-485 · Port 1</div>
              <button class="card-edit-btn" @click="router.push('/settings')" :title="t('edit_settings')">
                <SettingsIcon />
              </button>
            </div>
            <span class="pill ok" v-if="info!.rs485_1.is_busy"><span class="dot" />{{ t('active') }}</span>
            <span class="pill muted" v-else>{{ t('inactive') }}</span>
          </div>
          <div class="card-body">
            <RsStatus title="RS-485 1" :info="info!.rs485_1" :settings="settings!.rs485_1" />
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="card-title-row">
              <div class="title">RS-485 · Port 2</div>
              <button class="card-edit-btn" @click="router.push('/settings')" :title="t('edit_settings')">
                <SettingsIcon />
              </button>
            </div>
            <span class="pill ok" v-if="info!.rs485_2.is_busy"><span class="dot" />{{ t('active') }}</span>
            <span class="pill muted" v-else>{{ t('inactive') }}</span>
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
    "uptime_hours": "less than an hour | {n} hour | {n} hours | {n} hours",
    "uptime_minutes": "minute | {n} minute | {n} minutes | {n} minutes",
    "uptime_seconds": "second | {n} second | {n} seconds | {n} seconds",
    "firmware_version": "Firmware",
    "firmware_latest": "up to date",
    "firmware_latest_label": "latest",
    "firmware_update_btn": "Update"
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
    "uptime_hours": "- | {n} час | {n} часа | {n} часов",
    "uptime_minutes": "минута | {n} минута | {n} минуты | {n} минут",
    "uptime_seconds": "секунда | {n} секунда | {n} секунды | {n} секунд",
    "firmware_version": "Прошивка",
    "firmware_latest": "актуальная",
    "firmware_latest_label": "последняя",
    "firmware_update_btn": "Обновить"
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
    "uptime_hours": "бір сағаттан аз | {n} сағат | {n} сағат | {n} сағат",
    "uptime_minutes": "минут | {n} минут | {n} минут | {n} минут",
    "uptime_seconds": "секунд | {n} секунд | {n} секунд | {n} секунд",
    "firmware_version": "Бағдарлама",
    "firmware_latest": "өзекті",
    "firmware_latest_label": "соңғы",
    "firmware_update_btn": "Жаңарту"
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
    "uptime_hours": "meno di un'ora | {n} ora | {n} ore | {n} ore",
    "uptime_minutes": "minuto | {n} minuto | {n} minuti | {n} minuti",
    "uptime_seconds": "secondo | {n} secondo | {n} secondi | {n} secondi",
    "firmware_version": "Firmware",
    "firmware_latest": "aggiornato",
    "firmware_latest_label": "ultima",
    "firmware_update_btn": "Aggiorna"
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
    "uptime_hours": "weniger als eine Stunde | {n} Stunde | {n} Stunden | {n} Stunden",
    "uptime_minutes": "Minute | {n} Minute | {n} Minuten | {n} Minuten",
    "uptime_seconds": "Sekunde | {n} Sekunde | {n} Sekunden | {n} Sekunden",
    "firmware_version": "Firmware",
    "firmware_latest": "aktuell",
    "firmware_latest_label": "neueste",
    "firmware_update_btn": "Aktualisieren"
  }
}
</i18n>
