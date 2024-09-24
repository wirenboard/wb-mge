<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import ArrowRight from '@/assets/arrowRight.svg?component';
import ArrowLeft from '@/assets/arrowLeft.svg?component';
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

const convertToHexArray = (arr: number[]) => {
  return arr.map(num => '0x' + Math.abs(num).toString(16).padStart(2, '0')).toString().replaceAll(',', ', ');
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter">
      <Button @click="getData">{{ t('get_data') }}</Button>
      <Button @click="toggleAnalysis">{{ isAnalysing ? t('stop_analysis') : t('start_analysis') }}</Button>
    </Heading>

    <table class="traffic-table">
      <tbody>
      <tr>
        <th>Time</th>
        <th>Dir</th>
        <th>Slave id</th>
        <th>Func</th>
        <th>Data</th>
        <th>CRC</th>
      </tr>
      <tr v-for="(item, i) in packages" :key="i" :class="{ 'traffic-tableError': item.func === 70 }">
        <td>{{ item.time }}</td>
        <td>
          <ArrowRight v-if="item.dir === 'TX'" class="traffic-icon" />
          <ArrowLeft v-else class="traffic-icon" />
        </td>
        <td>{{ item.id }}</td>
        <td>{{ item.func }}</td>
        <td>[{{ convertToHexArray(item.data) }}]</td>
        <td :class="{ 'traffic-invalid': !item.crc_ok}">[{{ convertToHexArray(item.crc) }}]</td>
      </tr>
      </tbody>
    </table>
  </Layout>
</template>

<style scoped>
.traffic-icon {
  width: 20px;
  height: 20px;
}

.traffic-table {
  width: 100%;
}

.traffic-table td {
  vertical-align: center;
}

.traffic-tableError {
  background: #ff151545;
}

.traffic-invalid {
  color: var(--danger-color);
  font-weight: bold;
}
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
