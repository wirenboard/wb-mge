<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import RsSettings from '@/components/RsSettings.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <RsSettings
          v-model:settings="data.rs485_1"
          field="rs485_1"
          :title="t('port_1')"
          :sub="t('port1_sub')"
        />

        <div class="stack">
          <RsSettings
            v-model:settings="data.rs485_2"
            field="rs485_2"
            :title="t('port_2')"
            :sub="t('port2_sub')"
          />

          <section class="card">
            <form @submit.prevent="updateSettings({ io_bus: data.io_bus })">
              <div class="card-header">
                <div class="card-title-wrap">
                  <div class="title">{{ t('io_bus') }}</div>
                  <div class="sub">{{ t('io_bus_sub') }}</div>
                </div>
                <Button
                  type="submit"
                  :is-loading="isLoading && isChanged(['io_bus'])"
                  :disabled="isLoading || !isChanged(['io_bus'])"
                >
                  {{ t('save') }}
                </Button>
              </div>
              <div class="card-body">
                <div class="field">
                  <label for="io_bus">{{ t('io_bus_enable') }}</label>
                  <div class="field-switch"><Switch id="io_bus" v-model="data.io_bus" /></div>
                </div>
              </div>
            </form>
          </section>
</div>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
.field-switch {
  justify-self: end;
}
</style>

<i18n>
{
  "en": {
    "title": "Serial ports",
    "crumbs": "RS-485 interfaces",
    "save": "Save",
    "io_bus_sub": "WB-MIO chip connected to RS-485 Port 2. Default address 247.",
    "io_bus_enable": "Enable I/O Bus",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "port1_sub": "Wired terminal · left",
    "port2_sub": "Wired terminal · right + I/O bus"
  },
  "ru": {
    "title": "Последовательные порты",
    "crumbs": "Интерфейсы RS-485",
    "save": "Сохранить",
    "io_bus_sub": "Чип WB-MIO, подключённый к RS-485 Port 2. Адрес по умолчанию 247.",
    "io_bus_enable": "Включить I/O Bus",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "port1_sub": "Левый клеммник",
    "port2_sub": "Правый клеммник + I/O bus"
  },
  "kk": {
    "title": "Сериялық порттар",
    "crumbs": "RS-485 интерфейстері",
    "save": "Сақтау",
    "io_bus_sub": "RS-485 Port 2-ге қосылған WB-MIO чипі. Әдепкі адресі 247.",
    "io_bus_enable": "I/O Bus қосу",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "port1_sub": "Сымды клемма · сол",
    "port2_sub": "Сымды клемма · оң + I/O bus"
  },
  "it": {
    "title": "Porte seriali",
    "crumbs": "Interfacce RS-485",
    "save": "Salva",
    "io_bus_sub": "Chip WB-MIO collegato alla RS-485 Port 2. Indirizzo predefinito 247.",
    "io_bus_enable": "Abilita I/O Bus",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Porta 1",
    "port_2": "RS-485 · Porta 2",
    "port1_sub": "Morsettiera · sinistra",
    "port2_sub": "Morsettiera · destra + I/O bus"
  },
  "de": {
    "title": "Serielle Schnittstellen",
    "crumbs": "RS-485-Schnittstellen",
    "save": "Speichern",
    "io_bus_sub": "WB-MIO-Chip an RS-485 Port 2 angeschlossen. Standardadresse 247.",
    "io_bus_enable": "I/O Bus aktivieren",
    "io_bus": "I/O Bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "port1_sub": "Klemmenleiste · links",
    "port2_sub": "Klemmenleiste · rechts + I/O bus"
  }
}

</i18n>
