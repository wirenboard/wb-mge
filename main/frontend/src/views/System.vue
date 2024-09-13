<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { firmwareVersion } from '@/common/global';
import { Info } from '@/common/types';
import Button from '@/components/Button.vue';
import FileUpload from '@/components/FileUpload.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import { api } from '@/utils/api';
import SettingsActions from '@/components/SettingsActions.vue';

const { t } = useI18n();
const firmwareFile = ref();
const loadedMethod = ref();

const getFirmwareVersion = async () => {
  return api<Info>('info').then((res) => {
    firmwareVersion.value = res?.firmware;
  });
};

if (!firmwareVersion.value) {
  await getFirmwareVersion();
}

const updateFirmware = async () => {
  loadedMethod.value = 'firmware';
  await api('update', firmwareFile.value[0], true).catch(err => {
    console.log('err', err);
  });
  await getFirmwareVersion();
  loadedMethod.value = null;
  location.reload();
};

const cmd = async (command: string, confirmText?: string) => {
  if (confirmText) {
    const isConfirm = confirm(confirmText);
    if (!isConfirm) {
      return;
    }
  }

  loadedMethod.value = command;
  await api('cmd', { cmd: command }).catch(err => {
    console.log('err', err);
  });
  loadedMethod.value = null;
  location.reload();
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter" />

    <div class="system-container">
      <fieldset class="system-info">
        <legend>{{ t('firmware_update') }}</legend>

        <div>{{ t('current_firmware_version') }} {{ firmwareVersion }}</div>
        <FileUpload v-model="firmwareFile" :placeholder="t('choose_firmware')" :disabled="loadedMethod === 'firmware'" @upload="updateFirmware" />
      </fieldset>

      <fieldset class="system-info">
        <legend>{{ t('settings_backup') }}</legend>

        <SettingsActions />
      </fieldset>

      <fieldset class="system-info">
        <legend>{{ t('danger_zone') }}</legend>

        <Button type="button" variant="danger" :disabled="loadedMethod === 'reboot'" @click="cmd('reboot')">{{ t('reboot') }}</Button>
        <Button type="button" variant="danger" :disabled="loadedMethod === 'set_default_settings'" @click="cmd('set_default_settings', t('factory_reset_confirm'))">{{ t('set_default_settings') }}</Button>
      </fieldset>
    </div>
  </Layout>
</template>

<style scoped>
.system-container {
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

.system-info {
  page-break-inside: avoid;
  break-inside: avoid;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.system-info * {
  width: fit-content;
}
</style>

<i18n>
{
  "en": {
    "title": "System",
    "firmware_update": "Firmware update",
    "settings_backup": "Settings backup",
    "danger_zone": "Danger zone",
    "choose_firmware": "Choose firmware",
    "current_firmware_version": "Current version",
    "set_default_settings": "Factory reset",
    "factory_reset_confirm": "Are you sure you want to do a factory reset?",
    "reboot": "Reboot"
  }
}
</i18n>
