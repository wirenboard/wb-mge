<script setup lang="ts">
import { computed, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { onBeforeRouteLeave } from 'vue-router';
import Select from 'vue-multiselect';
import ReloadIcon from '@/assets/reload.svg?component';
import EyeIcon from '@/assets/eye.svg?component';
import EyeOffIcon from '@/assets/eyeOff.svg?component';
import { useWifi } from '@/common/network';
import { useSettings } from '@/common/settings';
import type { Settings, WiFiNetwork, WiFiSecuityProtocol } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import IpInput from '@/components/IpInput.vue';
import RsSettings from '@/components/RsSettings.vue';
import Switch from '@/components/Switch.vue';
import { onCustomValidation } from '@/utils/validation';

const { t } = useI18n();
const { data, initData, isChanged, isLoading, updateSettings } = useSettings();
const { wifi, isPolling, startPolling, stopPolling } = useWifi();
const showKnxDeviceAuth = ref(false);
const showKnxUserPass = ref(false);
const selectedWifi = ref();
const wifiSelect = ref();
const searhcValue = ref('');
const addedWifiNetworks = ref([]);
const computedWifiNetworks = computed(() => {
  const networks: Partial<WiFiNetwork>[] = [...addedWifiNetworks.value, ...wifi.value];
  // @ts-ignore
  if (data.value?.wifi.sta_ssid && !networks.find((item: WiFiNetwork) => item?.ssid === data.value?.wifi.sta_ssid)) {
    networks.unshift({ ssid: data.value?.wifi.sta_ssid });
  }
  return networks;
});

watch(() => data.value?.wifi.mode, () => {
  if (initData.value?.wifi.mode !== 'none') {
    startPolling();
  }
}, { immediate: true });

watch([() => data.value?.wifi.sta_ssid, () => wifi.value], () => {
  selectedWifi.value = computedWifiNetworks.value.find(item => item.ssid === data.value?.wifi.sta_ssid);
});

onBeforeRouteLeave(() => {
  stopPolling();
});

const securityProtocol: WiFiSecuityProtocol[] = ['open', 'wpa2_psk', 'wpa3_psk'];

const updateWifiSettings = async () => {
  const val: Partial<Settings['wifi']> = {
    mode: data.value!.wifi.mode,
  };

  if (data.value!.wifi.mode === 'ap') {
    val.ap_ssid = data.value!.wifi.ap_ssid;
    val.ap_auth = data.value!.wifi.ap_auth;
    val.ap_pass = data.value!.wifi.ap_auth === 'open' ? '' : data.value!.wifi.ap_pass;
    val.ap_ip_static = data.value!.wifi.ap_ip_static;
    val.ap_mask_static = data.value!.wifi.ap_mask_static;
    val.ap_gw_static = data.value!.wifi.ap_gw_static;
  } else if (data.value!.wifi.mode === 'sta') {
    val.sta_ssid = data.value?.wifi.sta_ssid;
    val.sta_auth = data.value!.wifi.sta_auth;
    val.sta_pass = val.sta_auth === 'open' ? '' : data.value!.wifi.sta_pass;
    val.sta_dhcpc = data.value!.wifi.sta_dhcpc;

    if (!val.sta_dhcpc) {
      val.sta_ip_static = data.value!.wifi.sta_ip_static;
      val.sta_gw_static = data.value!.wifi.sta_gw_static;
      val.sta_mask_static = data.value!.wifi.sta_mask_static;
    }
  }

  await updateSettings({ wifi: val });
};

const changeWifi = ({ ssid }: WiFiNetwork) => {
  data.value!.wifi.sta_ssid = ssid;
};

const addNetwork = () => {
  const val = { ssid: searhcValue.value };
  // @ts-ignore
  addedWifiNetworks.value.push(val);
  data.value!.wifi.sta_ssid = searhcValue.value;
  wifiSelect.value.deactivate();
  selectedWifi.value = val;
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" />

    <div v-if="data" class="settings">
      <fieldset>
        <legend>{{ t('ethernet') }}</legend>
        <form
          class="settings-info"
          @submit.prevent="updateSettings({
            ethernet: {
              dhcpc: data.ethernet.dhcpc,
              ip_static: data.ethernet.ip_static,
              mask_static: data.ethernet.mask_static,
              gw_static: data.ethernet.gw_static,
            }
          })">
          <label for="eth_dhcpc">{{ t('dhcp_client') }}</label>
          <div class="settings-data">
            <Switch
              id="eth_dhcpc"
              v-model="data.ethernet.dhcpc"
            />
          </div>

          <template v-if="!data.ethernet.dhcpc">
            <label for="eth_ip_static">{{ t('ip') }}</label>
            <div class="settings-data">
              <IpInput id="eth_ip_static" v-model="data.ethernet.ip_static" :disabled="data.ethernet.dhcpc" name="eth_ip_static" />
            </div>

            <label for="eth_gw_static">{{ t('gateway') }}</label>
            <div class="settings-data">
              <IpInput id="eth_gw_static" v-model="data.ethernet.gw_static" :disabled="data.ethernet.dhcpc" name="eth_gw_static" />
            </div>

            <label for="eth_mask_static">{{ t('mask') }}</label>
            <div class="settings-data">
              <IpInput id="eth_mask_static" v-model="data.ethernet.mask_static" :disabled="data.ethernet.dhcpc" name="eth_mask_static" />
            </div>
          </template>

          <Button
            class="settings-submit"
            type="submit"
            :is-loading="isLoading && isChanged(['ethernet'])"
            :disabled="isLoading || !isChanged(['ethernet'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>

      <fieldset>
        <legend>{{ t('wifi_settings') }}</legend>
        <form
          class="settings-info"
          @submit.prevent="updateWifiSettings">
          <label for="wifi_mode">{{ t('wifi_mode') }}</label>
          <div class="settings-data">
            <select id="wifi_mode" v-model="data.wifi.mode" class="settings-wifi" name="wifi_mode">
              <option value="none">{{ t('disabled') }}</option>
              <option value="ap">{{ t('ap') }}</option>
              <option value="sta">{{ t('sta') }}</option>
            </select>
          </div>

          <template v-if="data.wifi.mode === 'ap'">
            <label for="ap_ssid">{{ t('ssid') }}</label>
            <div class="settings-data">
              <input
                id="ap_ssid"
                v-model="data.wifi.ap_ssid"
                type="text"
                name="ap_ssid"
                pattern="^[\x20-\x7E]{1,31}$"
                minlength="1"
                maxlength="32"
                required
                @input="(ev) => onCustomValidation(ev, t('wrong_ssid_pattern'))"
              />
            </div>

            <label for="ap_auth">{{ t('wifi_pass_security') }}</label>
            <div class="settings-data">
              <select id="ap_auth" v-model="data.wifi.ap_auth" name="ap_auth">
                <option v-for="item in securityProtocol" :key="item" :value="item">{{ t(item) }}</option>
              </select>
            </div>

            <template v-if="data.wifi.ap_auth !== 'open'">
              <label for="ap_pass">{{ t('password') }}</label>
              <div class="settings-data">
                <input
                  id="ap_pass"
                  v-model="data.wifi.ap_pass"
                  required
                  :placeholder="t('pass_placeholder')"
                  pattern="[\x20-\x7E]{8,63}"
                  minlength="8"
                  maxlength="63"
                  type="password"
                  name="ap_ pass"
                  @input="(ev) => onCustomValidation(ev, t('wrong_pass_pattern'))"
                />
              </div>
            </template>

            <label for="ap_ip_static">{{ t('ip') }}</label>
            <div class="settings-data">
              <IpInput id="ap_ip_static" v-model="data.wifi.ap_ip_static" name="ap_ip_static" />
            </div>

            <label for="ap_mask_static">{{ t('mask') }}</label>

            <div class="settings-data">
              <IpInput id="ap_mask_static" v-model="data.wifi.ap_mask_static" name="ap_mask_static" />
            </div>

            <label for="ap_gw_static">{{ t('gateway') }}</label>
            <div class="settings-data">
              <IpInput id="ap_gw_static" v-model="data.wifi.ap_gw_static" name="ap_gw_static" />
            </div>
          </template>

          <template v-if="data.wifi.mode === 'sta'">
            <label for="sta_ssid">{{ t('ssid') }}</label>
            <div class="settings-dropdown">
              <Select
                ref="wifiSelect"
                v-model="selectedWifi"
                label="ssid"
                track-by="ssid"
                open-direction="bottom"
                :placeholder="isPolling ? (data?.wifi.sta_ssid || '') : ''"
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
                  <button type="button" class="settings-addWifiButton" @click="addNetwork">{{ t('add_ssid') }}</button>
                </template>
                <template #option="props">
                  <div
                    :class="{
                      'settings-externalNetwrok': !wifi.find((item) => item.ssid === props.option.ssid)
                    }">
                    {{ props.option.ssid }}
                  </div>
                </template>
              </Select>

              <Button variant="outline" type="button" class="settings-reloadButton" :disalbed="isPolling" @click="startPolling">
                <ReloadIcon
                  class="settings-reload"
                  :class="{
                    'settings-loading': isPolling
                  }"
                />
              </Button>
            </div>

            <label for="sta_auth">{{ t('wifi_pass_security') }}</label>
            <div class="settings-data">
              <select id="sta_auth" v-model="data.wifi.sta_auth" name="sta_auth">
                <option v-for="item in securityProtocol" :key="item" :value="item">{{ t(item) }}</option>
              </select>
            </div>

            <template v-if="data.wifi.sta_auth !== 'open'">
              <label for="sta_pass">{{ t('password') }}</label>
              <div class="settings-data">
                <input id="sta_pass" v-model="data.wifi.sta_pass" required :placeholder="t('pass_placeholder')" type="password" name="sta_pass" />
              </div>
            </template>

            <label for="sta_dhcpc">{{ t('dhcp_client') }}</label>
            <div class="settings-data">
              <Switch
                id="sta_dhcpc"
                v-model="data.wifi.sta_dhcpc"
              />
            </div>

            <template v-if="!data.wifi.sta_dhcpc">
              <label for="sta_ip_static">{{ t('ip') }}</label>
              <div class="settings-data">
                <IpInput id="sta_ip_static" v-model="data.wifi.sta_ip_static" name="sta_ip_static" />
              </div>

              <label for="sta_gw_static">{{ t('mask') }}</label>
              <div class="settings-data">
                <IpInput id="sta_mask_static" v-model="data.wifi.sta_mask_static" name="sta_mask_static" />
              </div>

              <label for="sta_mask_static">{{ t('gateway') }}</label>
              <div class="settings-data">
                <IpInput id="sta_gw_static" v-model="data.wifi.sta_gw_static" name="sta_gw_static" />
              </div>
            </template>
          </template>

          <Button
            class="settings-submit"
            type="submit"
            :is-loading="isLoading && isChanged(['wifi'])"
            :disabled="isLoading || !isChanged(['wifi'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>

      <RsSettings
        v-model:settings="data.rs485_1"
        field="rs485_1"
        title="RS-485"
        :has-ports-conflict="false"
      />

      <fieldset>
        <legend>KNX IP Secure</legend>
        <form
          class="settings-info"
          @submit.prevent="updateSettings({
            knx: {
              enabled: data.knx.enabled,
              port: data.knx.port,
              device_auth: data.knx.device_auth,
              user_pass: data.knx.user_pass,
            }
          })">
          <label for="knx_enabled">{{ t('knx_enabled') }}</label>
          <div class="settings-data">
            <Switch
              id="knx_enabled"
              v-model="data.knx.enabled"
            />
          </div>

          <template v-if="data.knx.enabled">
            <label for="knx_port">{{ t('knx_port') }}</label>
            <div class="settings-data">
              <input
                id="knx_port"
                v-model.number="data.knx.port"
                type="number"
                min="1"
                max="65535"
                name="knx_port"
              />
            </div>

            <label for="knx_device_auth">{{ t('knx_device_auth') }}</label>
            <div class="settings-data settings-password">
              <input
                id="knx_device_auth"
                v-model="data.knx.device_auth"
                :type="showKnxDeviceAuth ? 'text' : 'password'"
                name="knx_device_auth"
                :placeholder="t('knx_auth_placeholder')"
              />
              <button type="button" class="settings-eyeButton" @click="showKnxDeviceAuth = !showKnxDeviceAuth">
                <EyeIcon v-if="!showKnxDeviceAuth" />
                <EyeOffIcon v-else />
              </button>
            </div>

            <label for="knx_user_pass">{{ t('knx_user_pass') }}</label>
            <div class="settings-data settings-password">
              <input
                id="knx_user_pass"
                v-model="data.knx.user_pass"
                :type="showKnxUserPass ? 'text' : 'password'"
                name="knx_user_pass"
                :placeholder="t('knx_pass_placeholder')"
              />
              <button type="button" class="settings-eyeButton" @click="showKnxUserPass = !showKnxUserPass">
                <EyeIcon v-if="!showKnxUserPass" />
                <EyeOffIcon v-else />
              </button>
            </div>
          </template>

          <Button
            class="settings-submit"
            type="submit"
            :is-loading="isLoading && isChanged(['knx'])"
            :disabled="isLoading || !isChanged(['knx'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>
    </div>
  </Layout>
</template>

<style>
.settings {
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

.settings-info {
  display: grid;
  gap: 6px 24px;
  grid-template-columns: 1fr 1fr;
  align-items: center;
  justify-items: flex-start;
  page-break-inside: avoid;
  break-inside: avoid;
  line-height: 1em;
}

.settings-info div {
  height: 33px;
}

.settings-data {
  width: 100%;
  display: flex;
  justify-content: end;
}

.settings-dropdown {
  width: 100%;
  max-width: 100%;
  display: flex;
  gap: 6px;
}

.settings-reloadButton {
  padding: 6px 8px !important;
}

.settings-reload {
  width: 16px;
  height: 16px;
}

.settings-loading {
  animation: rotate 1s linear infinite;
}

.settings-addWifiButton {
  all: unset;
  width: 100%;
}

.settings-externalNetwrok::after {
  content: '⚠️';
  margin-left: 6px;
}

.settings-info label {
  min-height: 32px;
  display: flex;
  align-items: center;
}

.settings-password {
  position: relative;
}

.settings-password input {
  padding-right: 36px;
}

.settings-eyeButton {
  all: unset;
  position: absolute;
  right: 8px;
  top: 50%;
  transform: translateY(-50%);
  cursor: pointer;
  opacity: 0.5;
  display: flex;
  align-items: center;
}

.settings-eyeButton:hover {
  opacity: 1;
}

.settings-submit {
  margin-top: 14px;
}

.settings-wifi {
  width: 100%;
}
</style>

<i18n>
{
  "en": {
    "title": "Settings",

    "ethernet": "Ethernet",
    "dhcp_client": "DHCP client",
    "ip": "Static IP",
    "gateway": "Gateway",
    "mask": "Submask",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Mode",
    "wifi_pass_security": "Network protection",
    "open": "Unsecured",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",
    "add_ssid": "Add ssid",
    "wrong_ssid_pattern": "Enter a string of 1–31 characters: Latin letters, numbers, spaces, and special characters",
    "wrong_pass_pattern": "Enter a string of 8–63 characters: Latin letters, numbers, spaces, and special characters",

    "knx_enabled": "Enable KNX",
    "knx_port": "TCP Port",
    "knx_device_auth": "Device Auth Password",
    "knx_user_pass": "User Password",
    "knx_auth_placeholder": "Device authentication",
    "knx_pass_placeholder": "Commissioning password"
  },
  "ru": {
    "title": "Настройки",

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
    "wrong_ssid_pattern": "Введите строку из 1–31 символов: латиница, цифры, пробелы и спецсимволы",
    "wrong_pass_pattern": "Введите строку из 8–63 символов: латиница, цифры, пробелы и спецсимволы",

    "knx_enabled": "Включить KNX",
    "knx_port": "TCP порт",
    "knx_device_auth": "Пароль аутентификации",
    "knx_user_pass": "Пароль пользователя",
    "knx_auth_placeholder": "Device authentication",
    "knx_pass_placeholder": "Commissioning password"
  },
  "kk": {
    "title": "Баптаулар",

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
    "wrong_ssid_pattern": "1–31 таңба енгізіңіз: латын әріптері, сандар, бос орындар және арнайы таңбалар",
    "wrong_pass_pattern": "8–63 таңба енгізіңіз: латын әріптері, сандар, бос орындар және арнайы таңбалар",
    "knx_enabled": "Enable KNX",
    "knx_port": "TCP Port",
    "knx_device_auth": "Device Auth Password",
    "knx_user_pass": "User Password",
    "knx_auth_placeholder": "Device authentication",
    "knx_pass_placeholder": "Commissioning password"
  },
  "it": {
    "title": "Impostazioni",
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
    "wrong_ssid_pattern": "Inserisci una stringa di 1–31 caratteri: lettere latine, numeri, spazi e caratteri speciali",
    "wrong_pass_pattern": "Inserisci una stringa di 8–63 caratteri: lettere latine, numeri, spazi e caratteri speciali",
    "knx_enabled": "Enable KNX",
    "knx_port": "TCP Port",
    "knx_device_auth": "Device Auth Password",
    "knx_user_pass": "User Password",
    "knx_auth_placeholder": "Device authentication",
    "knx_pass_placeholder": "Commissioning password"
  },
  "de": {
    "title": "Einstellungen",
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
    "wrong_ssid_pattern": "Geben Sie eine Zeichenfolge mit 1–31 Zeichen ein: lateinische Buchstaben, Zahlen, Leerzeichen und Sonderzeichen",
    "wrong_pass_pattern": "Geben Sie eine Zeichenfolge mit 8–63 Zeichen ein: lateinische Buchstaben, Zahlen, Leerzeichen und Sonderzeichen",
    "knx_enabled": "KNX aktivieren",
    "knx_port": "TCP-Port",
    "knx_device_auth": "Geräte-Auth-Passwort",
    "knx_user_pass": "Benutzerpasswort",
    "knx_auth_placeholder": "Device authentication",
    "knx_pass_placeholder": "Commissioning password"
  }
}
</i18n>
