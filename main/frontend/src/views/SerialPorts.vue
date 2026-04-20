<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import RsSettings from '@/components/RsSettings.vue';

const { t } = useI18n();
const { data } = useSettings();
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <RsSettings
          v-model:settings="data.rs485_1"
          field="rs485_1"
          title="RS-485 · Port 1"
          sub="Wired terminal · left"
          :has-ports-conflict="data.rs485_1.bridge.port === data.rs485_2.bridge.port"
        />

        <RsSettings
          v-model:settings="data.rs485_2"
          v-model:io_bus="data.io_bus"
          field="rs485_2"
          title="RS-485 · Port 2"
          sub="Wired terminal · right + I/O bus"
          :has-ports-conflict="data.rs485_1.bridge.port === data.rs485_2.bridge.port"
        />
      </div>
    </div>
  </Layout>
</template>

<i18n>
{
  "en": { "title": "Serial ports", "crumbs": "RS-485 interfaces" },
  "ru": { "title": "Последовательные порты", "crumbs": "Интерфейсы RS-485" },
  "kk": { "title": "Сериялық порттар", "crumbs": "RS-485 интерфейстері" },
  "it": { "title": "Porte seriali", "crumbs": "Interfacce RS-485" },
  "de": { "title": "Serielle Schnittstellen", "crumbs": "RS-485-Schnittstellen" }
}
</i18n>
