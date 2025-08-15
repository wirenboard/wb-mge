<script setup lang="ts">
import { onUnmounted, ref, onMounted } from 'vue';
import { useI18n } from 'vue-i18n';
import { useWifi } from '@/common/network';
import { useSettings } from '@/common/settings';
import type { WiFiSecuityProtocol } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Info from '@/components/Info.vue';
import IpInput from '@/components/IpInput.vue';
import RsSettings from '@/components/RsSettings.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { data, initData, isChanged, isLoading, updateSettings } = useSettings();
const { wifi, startPolling, stopPolling } = useWifi();
const isChangeApPassword = ref(false);
const isChangeStaPassword = ref(false);

onMounted(() =>{
  startPolling();
});

onUnmounted(() => {
  stopPolling();
});

const securityProtocol: WiFiSecuityProtocol[] = ['open', 'wpa2_psk', 'wpa3_psk'];

const updateWifiSettings = async () => {
  const val: any = {
    mode: data.value!.wifi.mode,
    ap_ip_static: data.value!.wifi.ap_ip_static,
    ap_mask_static: data.value!.wifi.ap_mask_static,
    ap_gw_static: data.value!.wifi.ap_gw_static,
    ap_ssid: data.value!.wifi.ap_ssid,
    sta_auth: data.value!.wifi.sta_auth,
    sta_ssid: data.value!.wifi.sta_ssid,
    ap_auth: data.value!.wifi.ap_auth,
  };

  if (data.value!.wifi.ap_auth === 'open') {
    val.ap_pass = '';
  } else if (isChangeApPassword.value) {
    val.ap_pass = data.value!.wifi.ap_pass;
  }

  if (data.value!.wifi.sta_auth === 'open') {
    val.sta_pass = '';
  } else if (isChangeStaPassword.value) {
    val.sta_pass = data.value!.wifi.sta_pass;
  }
  await updateSettings({ wifi: val });
  isChangeApPassword.value = false;
  isChangeStaPassword.value = false;
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
          <label for="eth_dhcpc">{{ t('eth_dhcpc') }}</label>
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
              <option value="none">{{ t('none') }}</option>
              <option value="ap">{{ t('ap') }}</option>
              <option value="sta">{{ t('sta') }}</option>
              <option value="apsta">{{ t('apsta') }}</option>
            </select>
          </div>

          <template v-if="data.wifi.mode !== 'none'">
            <template v-if="data.wifi.mode === 'ap' || data.wifi.mode === 'apsta'">
              <template v-if="data.wifi.mode === 'apsta'">
                <b>{{ t('ap') }}</b>
                <div></div>
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

              <label for="ap_ssid">{{ t('ssid') }}</label>
              <div class="settings-data">
                <input id="ap_ssid" v-model="data.wifi.ap_ssid" type="text" name="ap_ssid" pattern="[\x20-\x7E]{1,32}" minlength="1" maxlength="32" required />
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
                  <button v-if="!isChangeApPassword" class="settings-textButton" type="button" @click="isChangeApPassword = true">{{ t('change_password') }}</button>
                  <input v-else id="ap_pass" v-model="data.wifi.ap_pass" v-focus required :placeholder="t('pass_placeholder')" pattern="[\x20-\x7E]{8,63}" minlength="8" maxlength="63" type="password" name="ap_ pass" />
                </div>
              </template>
            </template>

            <template v-if="data.wifi.mode === 'sta' || data.wifi.mode === 'apsta'">
              <template v-if="data.wifi.mode === 'apsta'">
                <b>{{ t('sta') }}</b>
                <div></div>
              </template>

              <label for="sta_ssid">{{ t('ssid') }}</label>
              <div class="settings-data">
                <input v-if="!wifi.length" id="sta_ssid" v-model="data.wifi.sta_ssid" type="text" name="sta_ssid" required />
                <select v-else id="sta_ssid" v-model="data.wifi.sta_ssid" class="settings-wifi" name="sta_ssid" @click="startPolling">
                  <option v-for="item in wifi" :key="item.ssid" :value="item.ssid">{{ item.ssid }}</option>
                </select>
              </div>
              <Info v-if="!['sta', 'apsta'].includes(initData!.wifi.mode)" :text="t('scan_info')" />

              <label for="sta_auth">{{ t('wifi_pass_security') }}</label>
              <div class="settings-data">
                <select id="sta_auth" v-model="data.wifi.sta_auth" name="sta_auth">
                  <option v-for="item in securityProtocol" :key="item" :value="item">{{ t(item) }}</option>
                </select>
              </div>

              <template v-if="data.wifi.sta_auth !== 'open'">
                <label for="sta_pass">{{ t('password') }}</label>
                <div class="settings-data">
                  <button v-if="!isChangeStaPassword" class="settings-textButton" type="button" @click="isChangeStaPassword = true">{{ t('change_password') }}</button>
                  <input v-else id="sta_pass" v-model="data.wifi.sta_pass" v-focus required :placeholder="t('pass_placeholder')" type="password" name="ap_ sta_pass" />
                </div>
              </template>
            </template>
          </template>

          <Button
            class="settings-submit"
            type="submit"
            :is-loading="isLoading && isChanged(['wifi'])"
            :disabled="isLoading || (!isChanged(['wifi']) && !isChangeStaPassword && !isChangeApPassword)"
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
  grid-template-columns: 45% 55%;
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

.settings-textButton {
  appearance: none;
  background: transparent;
  color: var(--text-color);
  text-decoration: underline;
}

.settings-textButton:hover,
.settings-textButton:focus{
  background: transparent !important;
  outline: none;
  box-shadow: none;
  color: var(--link-color);
}
</style>

<i18n>
{
  "en": {
    "title": "Settings",

    "ethernet": "Ethernet",
    "eth_dhcpc": "DHCP",
    "ip": "Static IP",
    "gateway": "Gateway",
    "mask": "Submask",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Mode",
    "wifi_pass_security": "Network protection",
    "scan_info": "Scanning will be available when the access point or access point and client mode is set and saved",
    "open": "Unsecured",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",

    "ssid": "Network name (SSID)"
  },
  "ru": {
    "title": "Настройки",

    "ethernet": "Ethernet",
    "eth_dhcpc": "DHCP",
    "ip": "IP",
    "gateway": "Шлюз",
    "mask": "Маска",

    "wifi_settings": "Wi-Fi",
    "wifi_mode": "Роль",
    "wifi_pass_security": "Защита сети",
    "scan_info": "Сканирование будет доступно когда будет установлен и сохранён режим точка доступа или точка доступа и клиент",
    "open": "Без защиты",
    "wpa2_psk": "WPA2-PSK",
    "wpa3_psk": "WPA3-PSK",

    "ssid": "Имя сети (SSID)"
  }
}
</i18n>
