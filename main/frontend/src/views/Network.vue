<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { onBeforeRouteLeave } from 'vue-router';
import Select from 'vue-multiselect';
import ReloadIcon from '@/assets/reload.svg?component';
import { useWifi } from '@/common/network';
import { useSettings } from '@/common/settings';
import type { Settings, WiFiNetwork, WiFiSecuityProtocol } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import InputNumber from '@/components/InputNumber.vue';
import IpInput from '@/components/IpInput.vue';
import PasswordInput from '@/components/PasswordInput.vue';
import Switch from '@/components/Switch.vue';
import { onCustomValidation } from '@/utils/validation';

const { t } = useI18n();
const { data, initData, isChanged, isLoading, updateSettings } = useSettings();
const { wifi, isPolling, startPolling, stopPolling } = useWifi();
const selectedWifi = ref();
const wifiSelect = ref();
const searhcValue = ref('');
const addedWifiNetworks = ref([]);
const computedWifiNetworks = computed(() => {
  const networks: Partial<WiFiNetwork>[] = [...addedWifiNetworks.value, ...wifi.value];
  // @ts-ignore
  if (data.value?.wifi?.sta_ssid && !networks.find((item: WiFiNetwork) => item?.ssid === data.value?.wifi?.sta_ssid)) {
    networks.unshift({ ssid: data.value?.wifi?.sta_ssid });
  }
  return networks;
});

watch(() => data.value?.wifi?.mode, () => {
  if (initData.value?.wifi?.mode !== 'none') {
    startPolling();
  }
}, { immediate: true });

watch([() => data.value?.wifi?.sta_ssid, () => wifi.value], () => {
  selectedWifi.value = computedWifiNetworks.value.find(item => item.ssid === data.value?.wifi?.sta_ssid);
});

onBeforeRouteLeave(() => {
  stopPolling();
});

const securityProtocol: WiFiSecuityProtocol[] = ['open', 'wpa2_psk', 'wpa3_psk'];

// Non-optional accessor used in the template; only rendered when !data.wifi_perm_disable (wifi is present)
const wifiSettings = computed(() => data.value!.wifi as NonNullable<Settings['wifi']>);

const updateWifiSettings = async () => {
  const val: Partial<Settings['wifi']> = {
    mode: data.value!.wifi!.mode,
  };

  if (data.value!.wifi!.mode === 'ap') {
    val.ap_ssid = data.value!.wifi!.ap_ssid;
    val.ap_auth = data.value!.wifi!.ap_auth;
    val.ap_pass = data.value!.wifi!.ap_auth === 'open' ? '' : data.value!.wifi!.ap_pass;
    val.ap_ip_static = data.value!.wifi!.ap_ip_static;
    val.ap_mask_static = data.value!.wifi!.ap_mask_static;
    val.ap_gw_static = data.value!.wifi!.ap_gw_static;
  } else if (data.value!.wifi!.mode === 'sta') {
    val.sta_ssid = data.value?.wifi?.sta_ssid;
    val.sta_auth = data.value!.wifi!.sta_auth;
    val.sta_pass = val.sta_auth === 'open' ? '' : data.value!.wifi!.sta_pass;
    val.sta_dhcpc = data.value!.wifi!.sta_dhcpc;

    if (!val.sta_dhcpc) {
      val.sta_ip_static = data.value!.wifi!.sta_ip_static;
      val.sta_gw_static = data.value!.wifi!.sta_gw_static;
      val.sta_mask_static = data.value!.wifi!.sta_mask_static;
    }
  }

  await updateSettings({ wifi: val });
};

const changeWifi = ({ ssid }: WiFiNetwork) => {
  data.value!.wifi!.sta_ssid = ssid;
};

// Posts the six mqtt_* keys and nothing else, the way every other card on this page posts only
// its own group.
const updateMqttSettings = () => updateSettings({
  mqtt_enabled: data.value!.mqtt_enabled,
  mqtt_host: data.value!.mqtt_host,
  mqtt_port: data.value!.mqtt_port,
  mqtt_user: data.value!.mqtt_user,
  mqtt_pass: data.value!.mqtt_pass,
  mqtt_id: data.value!.mqtt_id,
});

const mqttFields = ['mqtt_enabled', 'mqtt_host', 'mqtt_port', 'mqtt_user', 'mqtt_pass', 'mqtt_id'];

const addNetwork = () => {
  const val = { ssid: searhcValue.value };
  // @ts-ignore
  addedWifiNetworks.value.push(val);
  data.value!.wifi!.sta_ssid = searhcValue.value;
  wifiSelect.value.deactivate();
  selectedWifi.value = val;
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <!-- Ethernet and MQTT share a column, so the tall Wi-Fi card leaves no gap under Ethernet.
             Without Wi-Fi the wrapper dissolves and the two cards sit side by side. -->
        <div :class="data.wifi_perm_disable ? 'network-columnFlat' : 'stack'">
          <section class="card">
            <form
              @submit.prevent="updateSettings({
                ethernet: {
                  dhcpc: data.ethernet.dhcpc,
                  ip_static: data.ethernet.ip_static,
                  mask_static: data.ethernet.mask_static,
                  gw_static: data.ethernet.gw_static,
                }
              })">
              <div class="card-header">
                <div class="title">{{ t('ethernet') }}</div>
                <Button
                  type="submit"
                  :is-loading="isLoading && isChanged(['ethernet'])"
                  :disabled="isLoading || !isChanged(['ethernet'])"
                >
                  {{ t('save') }}
                </Button>
              </div>
              <div class="card-body">
                <div class="field">
                  <label for="eth_dhcpc">{{ t('dhcp_client') }}</label>
                  <div class="network-switchEnd"><Switch id="eth_dhcpc" v-model="data.ethernet.dhcpc" /></div>
                </div>

                <template v-if="!data.ethernet.dhcpc">
                  <div class="field">
                    <label for="eth_ip_static">{{ t('ip') }}</label>
                    <IpInput id="eth_ip_static" v-model="data.ethernet.ip_static" :disabled="data.ethernet.dhcpc" name="eth_ip_static" />
                  </div>
                  <div class="field">
                    <label for="eth_gw_static">{{ t('gateway') }}</label>
                    <IpInput id="eth_gw_static" v-model="data.ethernet.gw_static" :disabled="data.ethernet.dhcpc" name="eth_gw_static" />
                  </div>
                  <div class="field">
                    <label for="eth_mask_static">{{ t('mask') }}</label>
                    <IpInput id="eth_mask_static" v-model="data.ethernet.mask_static" :disabled="data.ethernet.dhcpc" name="eth_mask_static" />
                  </div>
                </template>
              </div>
            </form>
          </section>

          <section class="card">
            <form @submit.prevent="updateMqttSettings">
              <div class="card-header">
                <div class="title">{{ t('mqtt') }}</div>
                <Button
                  type="submit"
                  :is-loading="isLoading && isChanged(mqttFields)"
                  :disabled="isLoading || !isChanged(mqttFields)"
                >
                  {{ t('save') }}
                </Button>
              </div>
              <div class="card-body">
                <div class="field">
                  <label for="mqtt_enabled">{{ t('mqtt_enable') }}</label>
                  <div class="network-switchEnd"><Switch id="mqtt_enabled" v-model="data.mqtt_enabled" /></div>
                </div>
                <div class="field">
                  <label for="mqtt_host">{{ t('mqtt_host') }}</label>
                  <input
                    id="mqtt_host"
                    v-model="data.mqtt_host"
                    type="text"
                    name="mqtt_host"
                    class="mono"
                    pattern="^[\x21-\x7E]{0,63}$"
                    maxlength="63"
                    :placeholder="t('mqtt_host_placeholder')"
                    @input="(ev) => onCustomValidation(ev, t('wrong_mqtt_host_pattern'))"
                  />
                </div>
                <div class="field">
                  <label for="mqtt_port">{{ t('mqtt_port') }}</label>
                  <InputNumber id="mqtt_port" v-model="data.mqtt_port" name="mqtt_port" class="mono" min="1" max="65535" required />
                </div>
                <div class="field">
                  <label for="mqtt_user">{{ t('mqtt_user') }}</label>
                  <input
                    id="mqtt_user"
                    v-model="data.mqtt_user"
                    type="text"
                    name="mqtt_user"
                    autocomplete="off"
                    pattern="^[\x20-\x7E]{0,63}$"
                    maxlength="63"
                    @input="(ev) => onCustomValidation(ev, t('wrong_mqtt_credential_pattern'))"
                  />
                </div>
                <div class="field">
                  <label for="mqtt_pass">{{ t('mqtt_pass') }}</label>
                  <PasswordInput
                    id="mqtt_pass"
                    v-model="data.mqtt_pass"
                    name="mqtt_pass"
                    autocomplete="new-password"
                    pattern="^[\x20-\x7E]{0,63}$"
                    maxlength="63"
                    @input="(ev: Event) => onCustomValidation(ev, t('wrong_mqtt_credential_pattern'))"
                  />
                </div>
                <!-- The id is pasted straight into /devices/<id>/... , so the characters MQTT
                     gives a meaning of their own are refused here as well as by the firmware. -->
                <div class="field">
                  <label for="mqtt_id">{{ t('mqtt_id') }}</label>
                  <input
                    id="mqtt_id"
                    v-model="data.mqtt_id"
                    type="text"
                    name="mqtt_id"
                    class="mono"
                    pattern="^[\x21-\x22\x24-\x2A\x2C-\x2E\x30-\x7E]{0,63}$"
                    maxlength="63"
                    :placeholder="t('mqtt_id_placeholder')"
                    @input="(ev) => onCustomValidation(ev, t('wrong_mqtt_id_pattern'))"
                  />
                </div>
              </div>
            </form>
          </section>
        </div>

        <template v-if="!data.wifi_perm_disable">
        <section class="card">
          <form @submit.prevent="updateWifiSettings">
            <div class="card-header">
              <div class="title">{{ t('wifi_settings') }}</div>
              <Button
                type="submit"
                :is-loading="isLoading && isChanged(['wifi'])"
                :disabled="isLoading || !isChanged(['wifi'])"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="wifi_mode">{{ t('wifi_mode') }}</label>
                <select id="wifi_mode" v-model="wifiSettings.mode" name="wifi_mode">
                  <option value="none">{{ t('disabled') }}</option>
                  <option value="ap">{{ t('ap') }}</option>
                  <option value="sta">{{ t('sta') }}</option>
                </select>
              </div>

              <template v-if="wifiSettings.mode === 'ap'">
                <div class="field">
                  <label for="ap_ssid">{{ t('ssid') }}</label>
                  <input
                    id="ap_ssid"
                    v-model="wifiSettings.ap_ssid"
                    type="text"
                    name="ap_ssid"
                    pattern="^[\x20-\x7E]{1,31}$"
                    minlength="1"
                    maxlength="31"
                    required
                    @input="(ev) => onCustomValidation(ev, t('wrong_ssid_pattern'))"
                  />
                </div>
                <div class="field">
                  <label for="ap_auth">{{ t('wifi_pass_security') }}</label>
                  <select id="ap_auth" v-model="wifiSettings.ap_auth" name="ap_auth">
                    <option v-for="item in securityProtocol" :key="item" :value="item">{{ t(item) }}</option>
                  </select>
                </div>

                <template v-if="wifiSettings.ap_auth !== 'open'">
                  <div class="field">
                    <label for="ap_pass">{{ t('password') }}</label>
                    <PasswordInput
                      id="ap_pass"
                      v-model="wifiSettings.ap_pass"
                      required
                      :placeholder="t('pass_placeholder')"
                      pattern="[\x20-\x7E]{8,63}"
                      minlength="8"
                      maxlength="63"
                      name="ap_pass"
                      @input="(ev: Event) => onCustomValidation(ev, t('wrong_pass_pattern'))"
                    />
                  </div>
                </template>

                <div class="field">
                  <label for="ap_ip_static">{{ t('ip') }}</label>
                  <IpInput id="ap_ip_static" v-model="wifiSettings.ap_ip_static" name="ap_ip_static" />
                </div>
                <div class="field">
                  <label for="ap_mask_static">{{ t('mask') }}</label>
                  <IpInput id="ap_mask_static" v-model="wifiSettings.ap_mask_static" name="ap_mask_static" />
                </div>
                <div class="field">
                  <label for="ap_gw_static">{{ t('gateway') }}</label>
                  <IpInput id="ap_gw_static" v-model="wifiSettings.ap_gw_static" name="ap_gw_static" />
                </div>
              </template>

              <template v-if="wifiSettings.mode === 'sta'">
                <div class="field">
                  <label for="sta_ssid">{{ t('ssid') }}</label>
                  <div class="network-dropdown">
                    <Select
                      ref="wifiSelect"
                      v-model="selectedWifi"
                      label="ssid"
                      track-by="ssid"
                      open-direction="bottom"
                      :placeholder="isPolling ? (wifiSettings.sta_ssid || '') : ''"
                      :options="computedWifiNetworks"
                      :disabled="isPolling"
                      :max-height="600"
                      :allow-empty="false"
                      :show-labels="false"
                      :show-no-results="true"
                      use-teleport
                      @search-change="(val: string) => {
                        searhcValue = val;
                      }"
                      @select="changeWifi"
                    >
                      <template #noResult>
                        <button type="button" class="network-addWifiButton" @click="addNetwork">{{ t('add_ssid') }}</button>
                      </template>
                      <template #option="props">
                        <div
                          :class="{
                            'network-externalNetwork': !wifi.find((item) => item.ssid === props.option.ssid)
                          }">
                          {{ props.option.ssid }}
                        </div>
                      </template>
                    </Select>

                    <Button variant="outline" type="button" class="network-reloadButton" :aria-label="t('reload_networks')" :disabled="isPolling" @click="startPolling">
                      <ReloadIcon
                        class="network-reload"
                        :class="{
                          'network-loading': isPolling
                        }"
                      />
                    </Button>
                  </div>
                </div>

                <div class="field">
                  <label for="sta_auth">{{ t('wifi_pass_security') }}</label>
                  <select id="sta_auth" v-model="wifiSettings.sta_auth" name="sta_auth">
                    <option v-for="item in securityProtocol" :key="item" :value="item">{{ t(item) }}</option>
                  </select>
                </div>

                <template v-if="wifiSettings.sta_auth !== 'open'">
                  <div class="field">
                    <label for="sta_pass">{{ t('password') }}</label>
                    <PasswordInput
                      id="sta_pass"
                      v-model="wifiSettings.sta_pass"
                      required
                      :placeholder="t('pass_placeholder')"
                      pattern="[\x20-\x7E]{8,63}"
                      minlength="8"
                      maxlength="63"
                      name="sta_pass"
                      @input="(ev: Event) => onCustomValidation(ev, t('wrong_pass_pattern'))"
                    />
                  </div>
                </template>

                <div class="field">
                  <label for="sta_dhcpc">{{ t('dhcp_client') }}</label>
                  <div class="network-switchEnd"><Switch id="sta_dhcpc" v-model="wifiSettings.sta_dhcpc" /></div>
                </div>

                <template v-if="!wifiSettings.sta_dhcpc">
                  <div class="field">
                    <label for="sta_ip_static">{{ t('ip') }}</label>
                    <IpInput id="sta_ip_static" v-model="wifiSettings.sta_ip_static" name="sta_ip_static" />
                  </div>
                  <div class="field">
                    <label for="sta_gw_static">{{ t('mask') }}</label>
                    <IpInput id="sta_mask_static" v-model="wifiSettings.sta_mask_static" name="sta_mask_static" />
                  </div>
                  <div class="field">
                    <label for="sta_mask_static">{{ t('gateway') }}</label>
                    <IpInput id="sta_gw_static" v-model="wifiSettings.sta_gw_static" name="sta_gw_static" />
                  </div>
                </template>
              </template>
            </div>
          </form>
        </section>
        </template>
      </div>
    </div>
  </Layout>
</template>

<style>
.network-switchEnd {
  justify-self: end;
}

.network-columnFlat {
  display: contents;
}

.network-dropdown {
  width: 100%;
  max-width: 100%;
  display: flex;
  gap: 6px;
}

.network-reloadButton {
  padding: 6px 8px !important;
}

.network-reload {
  width: 16px;
  height: 16px;
}

.network-loading {
  animation: rotate 1s linear infinite;
}

.network-addWifiButton {
  all: unset;
  width: 100%;
}

.network-externalNetwork::after {
  content: '⚠️';
  margin-left: 6px;
}
</style>

<i18n>
{
  "en": {
    "title": "Network",
    "crumbs": "Ethernet & Wi-Fi configuration",

    "ethernet": "Ethernet",
    "dhcp_client": "DHCP client",
    "ip": "Static IP",
    "gateway": "Gateway",
    "mask": "Subnet mask",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Mode",
    "wifi_pass_security": "Network protection",
    "open": "Unsecured",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",
    "add_ssid": "Add ssid",
    "reload_networks": "Reload networks",
    "wrong_ssid_pattern": "Enter a string of 1–31 characters: Latin letters, numbers, spaces, and special characters",
    "wrong_pass_pattern": "Enter a string of 8–63 characters: Latin letters, numbers, spaces, and special characters",

    "mqtt": "MQTT",
    "mqtt_enable": "Enabled",
    "mqtt_host": "Broker address",
    "mqtt_host_placeholder": "Host name or IP address",
    "mqtt_port": "Port",
    "mqtt_user": "Username",
    "mqtt_pass": "Password",
    "mqtt_id": "Device ID",
    "mqtt_id_placeholder": "Model and MAC",
    "wrong_mqtt_host_pattern": "Enter up to 63 characters without spaces: Latin letters, numbers and special characters",
    "wrong_mqtt_credential_pattern": "Enter up to 63 characters: Latin letters, numbers, spaces, and special characters",
    "wrong_mqtt_id_pattern": "Enter up to 63 characters without spaces, / + and #: the ID becomes part of the topic"
  },
  "ru": {
    "title": "Сеть",
    "crumbs": "Настройка Ethernet и Wi-Fi",

    "ethernet": "Ethernet",
    "dhcp_client": "Клиент DHCP",
    "ip": "IP",
    "gateway": "Шлюз",
    "mask": "Маска",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Роль",
    "wifi_pass_security": "Защита сети",
    "open": "Без защиты",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",
    "add_ssid": "Добавить ssid",
    "reload_networks": "Обновить список сетей",
    "wrong_ssid_pattern": "Введите строку из 1–31 символов: латиница, цифры, пробелы и спецсимволы",
    "wrong_pass_pattern": "Введите строку из 8–63 символов: латиница, цифры, пробелы и спецсимволы",

    "mqtt": "MQTT",
    "mqtt_enable": "Включить",
    "mqtt_host": "Адрес брокера",
    "mqtt_host_placeholder": "Имя хоста или IP-адрес",
    "mqtt_port": "Порт",
    "mqtt_user": "Логин",
    "mqtt_pass": "Пароль",
    "mqtt_id": "Идентификатор устройства",
    "mqtt_id_placeholder": "Модель и MAC",
    "wrong_mqtt_host_pattern": "Введите до 63 символов без пробелов: латиница, цифры и спецсимволы",
    "wrong_mqtt_credential_pattern": "Введите до 63 символов: латиница, цифры, пробелы и спецсимволы",
    "wrong_mqtt_id_pattern": "Введите до 63 символов без пробелов, / + и #: идентификатор попадёт в топик"
  },
  "kk": {
    "title": "Желі",
    "crumbs": "Ethernet және Wi-Fi конфигурациясы",

    "ethernet": "Ethernet",
    "dhcp_client": "DHCP клиенті",
    "ip": "Тұрақты IP",
    "gateway": "Шлюз",
    "mask": "Маска",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Режим",
    "wifi_pass_security": "Желі қорғанысы",
    "open": "Қорғаныссыз",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",
    "add_ssid": "SSID қосу",
    "reload_networks": "Желілер тізімін жаңарту",
    "wrong_ssid_pattern": "1–31 таңба енгізіңіз: латын әріптері, сандар, бос орындар және арнайы таңбалар",
    "wrong_pass_pattern": "8–63 таңба енгізіңіз: латын әріптері, сандар, бос орындар және арнайы таңбалар",

    "mqtt": "MQTT",
    "mqtt_enable": "Қосу",
    "mqtt_host": "Брокер мекенжайы",
    "mqtt_host_placeholder": "Хост немесе IP",
    "mqtt_port": "Порт",
    "mqtt_user": "Логин",
    "mqtt_pass": "Құпия сөз",
    "mqtt_id": "Құрылғы идентификаторы",
    "mqtt_id_placeholder": "Модель және MAC",
    "wrong_mqtt_host_pattern": "Бос орынсыз 63 таңбаға дейін енгізіңіз: латын әріптері, сандар және арнайы таңбалар",
    "wrong_mqtt_credential_pattern": "63 таңбаға дейін енгізіңіз: латын әріптері, сандар, бос орындар және арнайы таңбалар",
    "wrong_mqtt_id_pattern": "Бос орынсыз, / + және # белгілерінсіз 63 таңбаға дейін енгізіңіз: идентификатор топикке кіреді"
  },
  "it": {
    "title": "Rete",
    "crumbs": "Configurazione Ethernet e Wi-Fi",

    "ethernet": "Ethernet",
    "dhcp_client": "Client DHCP",
    "ip": "IP statico",
    "gateway": "Gateway",
    "mask": "Maschera",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Modalità",
    "wifi_pass_security": "Protezione rete",
    "open": "Non protetto",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",
    "add_ssid": "Aggiungi SSID",
    "reload_networks": "Ricarica reti",
    "wrong_ssid_pattern": "Inserisci una stringa di 1–31 caratteri: lettere latine, numeri, spazi e caratteri speciali",
    "wrong_pass_pattern": "Inserisci una stringa di 8–63 caratteri: lettere latine, numeri, spazi e caratteri speciali",

    "mqtt": "MQTT",
    "mqtt_enable": "Abilitato",
    "mqtt_host": "Indirizzo del broker",
    "mqtt_host_placeholder": "Host o IP",
    "mqtt_port": "Porta",
    "mqtt_user": "Nome utente",
    "mqtt_pass": "Password",
    "mqtt_id": "ID del dispositivo",
    "mqtt_id_placeholder": "Modello e MAC",
    "wrong_mqtt_host_pattern": "Inserisci fino a 63 caratteri senza spazi: lettere latine, numeri e caratteri speciali",
    "wrong_mqtt_credential_pattern": "Inserisci fino a 63 caratteri: lettere latine, numeri, spazi e caratteri speciali",
    "wrong_mqtt_id_pattern": "Inserisci fino a 63 caratteri senza spazi, / + e #: l'ID entra a far parte del topic"
  },
  "de": {
    "title": "Netzwerk",
    "crumbs": "Ethernet- und Wi-Fi-Konfiguration",

    "ethernet": "Ethernet",
    "dhcp_client": "DHCP-Client",
    "ip": "Statische IP",
    "gateway": "Gateway",
    "mask": "Maske",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Modus",
    "wifi_pass_security": "Netzwerkschutz",
    "open": "Ungesichert",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",
    "add_ssid": "SSID hinzufügen",
    "reload_networks": "Netzwerke neu laden",
    "wrong_ssid_pattern": "Geben Sie eine Zeichenfolge mit 1–31 Zeichen ein: lateinische Buchstaben, Zahlen, Leerzeichen und Sonderzeichen",
    "wrong_pass_pattern": "Geben Sie eine Zeichenfolge mit 8–63 Zeichen ein: lateinische Buchstaben, Zahlen, Leerzeichen und Sonderzeichen",

    "mqtt": "MQTT",
    "mqtt_enable": "Aktiviert",
    "mqtt_host": "Broker-Adresse",
    "mqtt_host_placeholder": "Host oder IP",
    "mqtt_port": "Port",
    "mqtt_user": "Benutzername",
    "mqtt_pass": "Passwort",
    "mqtt_id": "Geräte-ID",
    "mqtt_id_placeholder": "Modell und MAC",
    "wrong_mqtt_host_pattern": "Geben Sie bis zu 63 Zeichen ohne Leerzeichen ein: lateinische Buchstaben, Zahlen und Sonderzeichen",
    "wrong_mqtt_credential_pattern": "Geben Sie bis zu 63 Zeichen ein: lateinische Buchstaben, Zahlen, Leerzeichen und Sonderzeichen",
    "wrong_mqtt_id_pattern": "Geben Sie bis zu 63 Zeichen ohne Leerzeichen, / + und # ein: die ID wird Teil des Topics"
  }
}
</i18n>
