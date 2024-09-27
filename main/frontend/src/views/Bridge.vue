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
  if (to.path === '/bridge') {
    await refresh();
  }
  next();
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter" />

    <div v-if="data" class="bridge-container">
      <fieldset class="bridge-fieldset">
        <form
          class="bridge-info"
          @submit.prevent="updateSettings({
            bridge_ip: data.bridge_ip,
            bridge_port: data.bridge_port,
            bridge_mode: data.bridge_mode,
            bridge_mb: data.bridge_mb
          }, t)">
          <label for="bridge_mode">{{ t('bridge_mode') }}</label>
          <select id="bridge_mode" v-model="data.bridge_mode" name="bridge_mode" autofocus>
            <option value="tcpc-serial">tcpc serial</option>
            <option value="tcps-serial">tcps serial</option>
          </select>

          <label for="bridge_ip">{{ t('bridge_ip') }}</label>
          <IpInput id="bridge_ip" v-model="data.bridge_ip" name="bridge_ip" />

          <label for="bridge_port">{{ t('bridge_port') }}</label>
          <input id="bridge_port" v-model="data.bridge_port" type="number" name="bridge_port" />

          <label for="bridge_mb">{{ t('bridge_mb') }}</label>
          <input id="bridge_mb" v-model="data.bridge_mb" type="checkbox" name="bridge_mb" />

          <Button
            class="bridge-submit"
            type="submit"
            :disabled="!isChanged(['bridge_ip', 'bridge_port', 'bridge_mode', 'bridge_mb'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>
    </div>
  </Layout>
</template>

<style scoped>
.bridge-container {
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

.bridge-fieldset {
  page-break-inside: avoid;
  break-inside: avoid;
}

.bridge-info {
  display: grid;
  gap: 6px 12px;
  grid-template-columns: fit-content(100px) fit-content(100px);
  align-items: center;
  justify-items: flex-start;
}

.bridge-info label {
  min-height: 32px;
  display: flex;
  align-items: center;
}

.bridge-submit {
  margin-top: 12px;
}
</style>

<i18n>
{
  "en": {
    "title": "Bridge configuration",
    "save": "Save",
    "bridge_ip": "IP",
    "bridge_port": "Port",
    "bridge_mode": "Mode",
    "bridge_mb": "Modbus"
  }
}
</i18n>
