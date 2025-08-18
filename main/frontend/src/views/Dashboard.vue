<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { firmwareVersion } from '@/common/global';
import { Info } from '@/common/types';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import { api } from '@/utils/api';

const { t } = useI18n();
const dataArray = ref<any[]>([[], [], []]);

const categorizeData = (key: string) => {
  if (key.startsWith('sta_') || key === 'con_sta') return 2;
  if (key.startsWith('eth_') || key === 'con_eth') return 1;
  return 0;
};

dataArray.value = await api<Info>('info').then((res) => {
  firmwareVersion.value = res.firmware;
  Object.entries(res).forEach(([key, value]) => {
    dataArray.value[categorizeData(key)].push({ [key]: value });
  });
  return dataArray.value;
});

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
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter" />

    <div class="dashboard">
      <fieldset class="dashboard-container">
        <legend>{{ t('common_info') }}</legend>
        <template v-for="(item, key) in dataArray[0]" :key="key">
          <div>{{ t(Object.keys(item)[0]) }}</div>
          <div>{{ getDisplayValue(Object.values(item)[0]) }}</div>
        </template>
      </fieldset>

      <fieldset class="dashboard-container">
        <legend>{{ t('ethernet') }}</legend>
        <template v-for="(item, key) in dataArray[1]" :key="key">
          <div>{{ t(Object.keys(item)[0]) }}</div>
          <div>{{ getDisplayValue(Object.values(item)[0]) }}</div>
        </template>
      </fieldset>

      <fieldset class="dashboard-container">
        <legend>{{ t('station') }}</legend>
        <template v-for="(item, key) in dataArray[2]" :key="key">
          <div>{{ t(Object.keys(item)[0]) }}</div>
          <div>{{ getDisplayValue(Object.values(item)[0]) }}</div>
        </template>
      </fieldset>
    </div>
  </Layout>
</template>

<style scoped>
.dashboard {
  column-count: 3;
  column-gap: 12px;

  @media (max-width: 1320px) {
    column-count: 2;
  }

  @media (max-width: 936px) {
    column-count: 1;
    width: fit-content;
  }

  @media (max-width: 500px) {
    width: 100%;
  }
}

.dashboard-container {
  display: grid;
  gap: 6px 24px;
  grid-template-columns: fit-content(180px) fit-content(100px);
  align-items: center;
  justify-items: flex-start;
  page-break-inside: avoid;
  break-inside: avoid;
}
</style>

<i18n>
{
  "en": {
    "title": "Dashboard",
    "common_info": "Common info",
    "ethernet": "Ethernet",
    "station": "Station",
    "device_name": "Device name",
    "firmware": "Firmware",
    "hardware": "Hardware",
    "serial_num": "Serial number  ",
    "con_eth": "Connection",
    "eth_ip": "IP",
    "eth_mask": "Submask",
    "eth_gw": "Gateway",
    "eth_mac": "MAC address",
    "con_sta": "Connection",
    "sta_ip": "IP",
    "sta_mask": "Submask",
    "sta_gw": "Gateway",
    "enabled": "Enabled",
    "disabled": "Disabled"
  }
}
</i18n>
