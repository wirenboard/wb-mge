<script setup lang="ts">
import { onUnmounted, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import Loader from '@/assets/loader.svg?component';
import { useWifi } from '@/common/network';
import { useSettings } from '@/common/settings';
import type { Settings, WiFiSecuityProtocol } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Info from '@/components/Info.vue';
import IpInput from '@/components/IpInput.vue';
import RsSettings from '@/components/RsSettings.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { data, initData, isChanged, isLoading, updateSettings } = useSettings();
const { wifi, isPolling, startPolling, stopPolling } = useWifi();

watch(() => data.value?.wifi.mode, () => {
  if (initData.value?.wifi.mode !== 'none') {
    startPolling();
  }
}, { immediate: true });

onUnmounted(() => {
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
    val.sta_ssid = data.value!.wifi.sta_ssid;
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
              <input id="ap_ssid" v-model="data.wifi.ap_ssid" type="text" name="ap_ssid" pattern="^[\x20-\x7E]{1,31}$" minlength="1" maxlength="32" required />
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
                <input id="ap_pass" v-model="data.wifi.ap_pass" v-focus required :placeholder="t('pass_placeholder')" pattern="[\x20-\x7E]{8,63}" minlength="8" maxlength="63" type="password" name="ap_ pass" />
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
            <div class="settings-data">
              <div v-if="!wifi.length" class="settings-wifiInputWrapper">
                <input id="sta_ssid" v-model="data.wifi.sta_ssid" class="settings-wifiInput" type="text" pattern="^[\x20-\x7E]{1,31}$" name="sta_ssid" required />
                <Loader v-if="isPolling" class="settings-loader" fill="currentColor" />
              </div>
              <select v-else id="sta_ssid" v-model="data.wifi.sta_ssid" class="settings-wifi" name="sta_ssid" @click="startPolling">
                <option v-for="item in wifi" :key="item.ssid" :value="item.ssid">{{ item.ssid }}</option>
              </select>
            </div>

            <Info v-if="initData!.wifi.mode === 'none'" :text="t('scan_info')" />

            <label for="sta_auth">{{ t('wifi_pass_security') }}</label>
            <div class="settings-data">
              <select id="sta_auth" v-model="data.wifi.sta_auth" name="sta_auth">
                <option v-for="item in securityProtocol" :key="item" :value="item">{{ t(item) }}</option>
              </select>
            </div>

            <template v-if="data.wifi.sta_auth !== 'open'">
              <label for="sta_pass">{{ t('password') }}</label>
              <div class="settings-data">
                <input id="sta_pass" v-model="data.wifi.sta_pass" v-focus required :placeholder="t('pass_placeholder')" type="password" name="sta_pass" />
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
                <IpInput id="sta_gw_static" v-model="data.wifi.sta_gw_static" name="sta_gw_static" />
              </div>

              <label for="sta_mask_static">{{ t('gateway') }}</label>
              <div class="settings-data">
                <IpInput id="sta_mask_static" v-model="data.wifi.sta_mask_static" name="sta_mask_static" />
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
        title="RS-485 1"
        :has-ports-conflict="data.rs485_1.bridge.port === data.rs485_2.bridge.port"
      />

      <RsSettings
        v-model:settings="data.rs485_2"
        v-model:io_bus="data.io_bus"
        field="rs485_2"
        title="RS-485 2"
        :has-ports-conflict="data.rs485_1.bridge.port === data.rs485_2.bridge.port"
      />
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
  grid-template-columns: 48% 52%;
  align-items: center;
  justify-items: flex-start;
  page-break-inside: avoid;
  break-inside: avoid;
}

.settings-info div {
  height: 33px;
}

.settings-info div:nth-child(odd),
.settings-data {
  width: calc(100% - 24px);
  display: flex;
  justify-content: end;
}

.settings-info label {
  min-height: 32px;
  display: flex;
  align-items: center;
}

.settings-submit {
  margin-top: 14px;
}

.settings-wifi {
  width: 100%;
}

.settings-wifiInputWrapper {
  position: relative;
  width: 100% !important;
}

.settings-wifiInput {
  width: 100% !important;
}

.settings-loader {
  min-width: 16px;
  height: 16px;
  position: absolute;
  top: 8px;
  right: -6px;
  z-index: 1000;
  animation: rotate 1s linear infinite;
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
    "scan_info": "Scanning will be available when the access point or client mode is set and saved",
    "open": "Unsecured",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK"
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
    "scan_info": "Сканирование будет доступно когда будет установлен и сохранён режим точка доступа или клиент",
    "open": "Без защиты",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK"
  }
}
</i18n>
