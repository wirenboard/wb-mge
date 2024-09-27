<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useRouter } from 'vue-router';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import IpInput from '@/components/IpInput.vue';

const { t } = useI18n();
const router = useRouter();
const { data, isChanged, updateSettings, refresh } = await useSettings();

router.beforeResolve(async (to, from, next) => {
  if (to.path === '/network') {
    await refresh();
  }
  next();
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter" />

    <div v-if="data" class="network-container">
      <fieldset class="network-fieldset">
        <legend>{{ t('ethernet') }}</legend>
        <form
          class="network-info"
          @submit.prevent="updateSettings({
            hostname: data.hostname,
            eth_dhcpc: data.eth_dhcpc,
            eth_ip_static: data.eth_ip_static,
            eth_mask_static: data.eth_mask_static,
            eth_gw_static: data.eth_gw_static,
          }, t)">
          <label for="hostname">{{ t('hostname') }}</label>
          <input id="hostname" v-model="data.hostname" type="text" name="hostname" autofocus />

          <label for="eth_dhcpc">{{ t('eth_dhcpc') }}</label>
          <input id="eth_dhcpc" v-model="data.eth_dhcpc" type="checkbox" name="eth_dhcpc" />

          <template v-if="!data.eth_dhcpc">
            <label for="eth_ip_static">{{ t('eth_ip_static') }}</label>
            <IpInput id="eth_ip_static" v-model="data.eth_ip_static" name="eth_ip_static" />

            <label for="eth_mask_static">{{ t('eth_mask_static') }}</label>
            <IpInput id="eth_mask_static" v-model="data.eth_mask_static" name="eth_mask_static" />

            <label for="eth_gw_static">{{ t('eth_gw_static') }}</label>
            <IpInput id="eth_gw_static" v-model="data.eth_gw_static" name="eth_gw_static" />
          </template>

          <Button
            class="network-submit"
            type="submit"
            :disabled="!isChanged(['hostname', 'eth_dhcpc', 'eth_ip_static', 'eth_mask_static', 'eth_gw_static'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>

      <fieldset class="network-fieldset">
        <legend>{{ t('wifi_settings') }}</legend>
        <form
          class="network-info"
          @submit.prevent="updateSettings({
            wifi_mode: data.wifi_mode,
            ap_ip_static: data.ap_ip_static,
            ap_mask_static: data.ap_mask_static,
            ap_gw_static: data.ap_gw_static,
            ap_ssid: data.ap_ssid,
            ap_pass: data.ap_pass,
            sta_pass: data.sta_pass,
            sta_ssid: data.sta_ssid,
          }, t)">
          <label for="wifi_mode">{{ t('wifi_mode') }}</label>
          <select id="wifi_mode" v-model="data.wifi_mode" name="wifi_mode">
            <option value="none">None</option>
            <option value="ap">Access Point</option>
            <option value="sta">Station</option>
            <option value="apsta">Access Point & Station</option>
          </select>

          <template v-if="data.wifi_mode !== 'none'">
            <template v-if="data.wifi_mode === 'ap' || data.wifi_mode === 'apsta'">
              <label for="ap_ip_static">{{ t('ap_ip_static') }}</label>
              <IpInput id="ap_ip_static" v-model="data.ap_ip_static" name="ap_ip_static" />

              <label for="ap_mask_static">{{ t('ap_mask_static') }}</label>
              <IpInput id="ap_mask_static" v-model="data.ap_mask_static" name="ap_mask_static" />

              <label for="ap_gw_static">{{ t('ap_gw_static') }}</label>
              <IpInput id="ap_gw_static" v-model="data.ap_gw_static" name="ap_gw_static" />

              <label for="ap_ssid">{{ t('ap_ssid') }}</label>
              <input id="ap_ssid" v-model="data.ap_ssid" type="text" name="ap_ssid" />

              <label for="ap_pass">{{ t('ap_pass') }}</label>
              <input id="ap_pass" v-model="data.ap_pass" type="password" name="ap_pass" />
            </template>

            <template v-if="data.wifi_mode === 'sta' || data.wifi_mode === 'apsta'">
              <label for="sta_ssid">{{ t('sta_ssid') }}</label>
              <input id="sta_ssid" v-model="data.sta_ssid" type="text" name="sta_ssid" />

              <label for="sta_pass">{{ t('sta_pass') }}</label>
              <input id="sta_pass" v-model="data.sta_pass" type="password" name="sta_pass" />
            </template>
          </template>

          <Button
            class="network-submit"
            type="submit"
            :disabled="!isChanged(['wifi_mode', 'ap_ip_static', 'ap_mask_static', 'ap_gw_static', 'ap_ssid', 'ap_pass', 'sta_pass', 'sta_ssid'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>
    </div>
  </Layout>
</template>

<style scoped>
.network-container {
  column-count: 3;
  column-gap: 12px;

  @media (max-width: 1320px) {
    column-count: 2;
  }

  @media (max-width: 936px) {
    column-count: 1;
    width: fit-content;
  }
}

.network-fieldset {
  page-break-inside: avoid;
  break-inside: avoid;
}

.network-info {
  display: grid;
  gap: 6px 12px;
  grid-template-columns: fit-content(100px) fit-content(100px);
  align-items: center;
  justify-items: flex-start;
}

.network-info label {
  min-height: 32px;
  display: flex;
  align-items: center;
}

.network-submit {
  margin-top: 12px;
}
</style>

<i18n>
{
  "en": {
    "title": "Network settings",
    "save": "Save",
    "data_updated": "Data updated",
    "invalid_fields": "Fields were not updated: ",
    "ethernet": "Ethernet",
    "wifi_settings": "Wi-Fi",
    "serial": "Serial",
    "eth_dhcpc": "DHCP",
    "hostname": "Hostname",
    "wifi_mode": "Wi-Fi mode",
    "eth_ip_static": "Static IP",
    "eth_mask_static": "Submask",
    "eth_gw_static": "Gateway",
    "ap_ip_static": "AP Static IP",
    "ap_gw_static": "AP Gateway",
    "ap_ssid": "AP SSID",
    "ap_pass": "AP Password",
    "ap_mask_static": "AP Submask",
    "sta_ssid": "STA SSID",
    "sta_pass": "STA Password"
  }
}
</i18n>
