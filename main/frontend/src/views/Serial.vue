<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useRouter } from 'vue-router';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import InputNumber from '@/components/InputNumber.vue';
import Layout from '@/components/Layout.vue';

const { t } = useI18n();
const router = useRouter();
const { data, isChanged, isLoading, updateSettings, refresh } = await useSettings();

router.beforeResolve(async (to, from, next) => {
  if (to.path === '/serial') {
    await refresh();
  }
  next();
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter" />

    <div v-if="data" class="serial-container">
      <fieldset class="serial-fieldset">
        <form
          class="serial-info"
          @submit.prevent="updateSettings({
            baudrate: data.baudrate,
            stopbits: data.stopbits,
            parity: data.parity,
            databits: data.databits
          }, t)">
          <label for="baudrate">{{ t('baudrate') }}</label>
          <InputNumber id="baudrate" v-model="data.baudrate" name="baudrate" min="300" max="460800" required autofocus />

          <label for="parity">{{ t('parity') }}</label>
          <select id="parity" v-model="data.parity" name="parity">
            <option value="none">None</option>
            <option value="even">Even</option>
            <option value="odd">Odd</option>
          </select>

          <label for="databits">{{ t('databits') }}</label>
          <select id="databits" v-model="data.databits" name="databits">
            <option value="5-bit">5 bit</option>
            <option value="6-bit">6 bit</option>
            <option value="7-bit">7 bit</option>
            <option value="8-bit">8 bit</option>
          </select>

          <label for="stopbits">{{ t('stopbits') }}</label>
          <select id="stopbits" v-model="data.stopbits" name="stopbits">
            <option value="1-bit">1 bit</option>
            <option value="1.5-bit">1.5 bit</option>
            <option value="2-bit">2 bit</option>
          </select>

          <Button
            class="serial-submit"
            type="submit"
            :is-loading="isLoading"
            :disabled="isLoading || !isChanged(['baudrate', 'stopbits', 'parity', 'databits'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>
    </div>
  </Layout>
</template>

<style scoped>
.serial-container {
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

.serial-info {
  display: grid;
  gap: 6px 12px;
  grid-template-columns: fit-content(100px) fit-content(100px);
  align-items: center;
  justify-items: flex-start;
}

.serial-fieldset {
  page-break-inside: avoid;
  break-inside: avoid;
}

.serial-info label {
  min-height: 32px;
  display: flex;
  align-items: center;
}

.serial-submit {
  margin-top: 12px;
}
</style>

<i18n>
{
  "en": {
    "title": "Serial configuration",
    "baudrate": "Baud rate",
    "databits": "Data bits",
    "stopbits": "Stop bits",
    "parity": "Parity"
  }
}
</i18n>
