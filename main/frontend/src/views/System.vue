<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { alertData, firmwareVersion } from '@/common/global';
import { Info } from '@/common/types';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import FileUpload from '@/components/FileUpload.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import SettingsActions from '@/components/SettingsActions.vue';
import { api } from '@/utils/api';

const { t } = useI18n();
const firmwareFile = ref();
const loadedMethod = ref();
const { data, isChanged, updateSettings } = await useSettings();

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
  alertData.value = { type: 'success', message: t('firmware_update_processed') };

  await api('update', firmwareFile.value[0], true);
  // TODO add firmware update handler
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
  await api('cmd', { cmd: command });
  loadedMethod.value = null;
  location.reload();
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" info-link="https://wirenboard.com/wiki/WB-MGE_v.3_Modbus-Ethernet_Interface_Converter" />

    <div class="system-container">
      <fieldset class="network-fieldset">
        <legend>{{ t('credentials') }}</legend>
        <form
          class="system-infoInputs"
          @submit.prevent="updateSettings({
            login: data.login,
            pass: data.pass,
          }, t)">
          <label for="login">{{ t('login') }}</label>
          <input id="login" v-model="data.login" type="text" name="login" required />

          <label for="pass">{{ t('pass') }}</label>
          <input id="pass" v-model="data.pass" type="password" name="pass" />

          <Button
            class="network-submit"
            type="submit"
            :disabled="!isChanged(['login', 'pass'])"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>

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

  @media (max-width: 500px) {
    width: 100%;
  }
}

.system-info {
  page-break-inside: avoid;
  break-inside: avoid;
  display: flex;
  flex-direction: column;
  gap: 12px;
}

.system-infoInputs {
  display: grid;
  gap: 6px 12px;
  grid-template-columns: fit-content(100px) fit-content(100px);
  align-items: center;
  justify-items: flex-start;
}

.system-info * {
  width: fit-content;
}
</style>

<i18n>
{
  "en": {
    "title": "System",
    "credentials": "Credentials",
    "login": "Login",
    "pass": "Password",
    "firmware_update": "Firmware update",
    "settings_backup": "Settings backup",
    "danger_zone": "Danger zone",
    "choose_firmware": "Choose firmware",
    "current_firmware_version": "Current version",
    "firmware_update_processed": "Firmware update in progress",
    "set_default_settings": "Factory reset",
    "factory_reset_confirm": "Are you sure you want to do a factory reset?",
    "reboot": "Reboot"
  }
}
</i18n>
