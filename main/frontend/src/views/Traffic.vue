<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { Packgages } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import { api } from '@/utils/api';

const { t } = useI18n();
const packages = ref<Packgages['packages']>();
const isAnalysing = ref(false);

packages.value = await api<Packgages>('cmd', { cmd: 'get_analysis_data' }).then(res => {
  isAnalysing.value = res.result;
  return res.packages;
});

const getData = async () => {
  packages.value = await api<Packgages>('cmd', { cmd: 'get_analysis_data' }).then(res => {
    return res.packages;
  });
};

const toggleAnalysis = async () => {
  await api('cmd', { cmd: isAnalysing.value ? 'stop_analysis' : 'start_analysis' });
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter">
      <Button @click="getData">{{ t('get_data') }}</Button>
      <Button @click="toggleAnalysis">{{ isAnalysing ? t('stop_analysis') : t('start_analysis') }}</Button>
    </Heading>

    <table>
      <tbody>
      <tr>
        <th>time</th>
        <th>dir</th>
        <th>slave id</th>
        <th>func</th>
        <th>data</th>
        <th>CRC</th>
        <th>CRC is valid</th>
      </tr>
      <tr v-for="(item, i) in packages" :key="i">
        <td>{{ item.time }}</td>
        <td>{{ item.dir }}</td>
        <td>{{ item.id }}</td>
        <td>{{ item.func }}</td>
        <td>{{ item.data }}</td>
        <td>{{ item.crc }}</td>
        <td>{{ item.crc_ok }}</td>
      </tr>
      </tbody>
    </table>
  </Layout>
</template>

<style scoped>
</style>

<i18n>
{
  "en": {
    "title": "Traffic analysis",
    "get_data": "Get analysis data",
    "start_analysis": "Start analysis",
    "stop_analysis": "Stop analysis"
  }
}
</i18n>
