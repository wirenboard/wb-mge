<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { changeLang, type Locale } from '@/i18n';
import SaveIcon from '@/assets/save.svg?component';
import { useAlerts } from '@/common/alert';
import { useInfo } from '@/common/info';
import { documentation, email, support, website } from '@/common/links';
import { useSettings } from '@/common/settings';
import { useUptime } from '@/common/uptime';
import Button from '@/components/Button.vue';
import Configuration from '@/components/Configuration.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InputNumber from '@/components/InputNumber.vue';
import FileUpload from '@/components/FileUpload.vue';
import Layout from '@/components/Layout.vue';
import { api } from '@/utils/api';

const { t, locale } = useI18n();
const language = ref<Locale>(locale.value as Locale);
const firmwareFile = ref();
const loadedMethod = ref();
const isChangePassword = ref(false);
const { showAlert } = useAlerts();
const { data: settings, isChanged, updateSettings } = useSettings();
const { isReconnecting, uptime } = useUptime();
const { info } = useInfo();

const updateFirmware = async () => {
  loadedMethod.value = 'firmware';
  showAlert(t('firmware_update_processed'), { type: 'success' });

  await api('update', firmwareFile.value[0], true);
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
  isReconnecting.value = true;
  loadedMethod.value = null;
  setTimeout(() => {
    location.reload();
  }, 3500);
};

const updateInterface = () => {
  const val: any = {
    login: settings.value!.login,
  };

  if (isChangePassword.value) {
    val.pass = settings.value!.pass;
  }

  updateSettings(val);
  isChangePassword.value = false;
  changeLang(language.value);
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" />

    <div class="system">
      <fieldset class="system-container">
        <legend>{{ t('device') }}</legend>
        <div>{{ t('hostname') }}</div>
        <form class="system-data system-saveWrapper" @submit.prevent="updateSettings({ hostname: settings!.hostname })">
          <input v-model="settings!.hostname" type="text" name="hostname">
          <button type="submit" :disabled="!settings!.hostname || !isChanged(['hostname'])">
            <SaveIcon class="system-save" />
          </button>
        </form>

        <div>{{ t('serial_num') }}</div>
        <div class="system-data">
          {{ info!.serial_num }}
        </div>

        <div>{{ t('uptime') }}</div>
        <div class="system-data">
          <template v-if="uptime.days">
            <span class="system-uptime">{{ t('uptime_days', { n: uptime.days }) }}</span>
          </template>
          <template v-if="uptime.hours">
            <span class="system-uptime">{{ t('uptime_hours', { n: uptime.hours }) }}</span>
          </template>
          <span>{{ t('uptime_minutes', { n: uptime.minutes }) }}</span>
        </div>

        <div>{{ t('firmware_version') }}</div>
        <div class="system-data">
          {{ info?.firmware }}
        </div>

        <div>{{ t('firmware_update') }}</div>
        <div class="system-data">
          <FileUpload v-model="firmwareFile" :placeholder="t('choose_firmware')" :disabled="loadedMethod === 'firmware'" @upload="updateFirmware" />
        </div>
        <Info v-if="firmwareFile" :text="t('wirmware_update_info')" />

        <div>{{ t('reboot') }}</div>
        <div class="system-data">
          <Button type="button" variant="danger" :disabled="loadedMethod === 'reboot'" @click="cmd('reboot')">{{ t('restart') }}</Button>
        </div>
      </fieldset>

      <fieldset>
        <legend>{{ t('interface') }}</legend>
        <form
          class="system-container"
          @submit.prevent="updateInterface">
          <label for="port">{{ t('port') }}</label>
          <div class="system-data">
            <InputNumber id="port" v-model="settings!.web_port" type="text" name="port" autocomplete="port" required />
          </div>

          <label for="username">{{ t('login') }}</label>
          <div class="system-data">
            <input id="username" v-model="settings!.login" type="text" name="username" autocomplete="username" required />
          </div>

          <label for="new-password">{{ t('password') }}</label>
          <div class="system-data">
            <button v-if="!isChangePassword" class="system-textButton" type="button" @click="isChangePassword = true">{{ t('change_password') }}</button>
            <input v-else id="new-password" v-model="settings!.pass" v-focus required :placeholder="t('pass_placeholder')" autocomplete="new-password" type="password" name="new-password" />
          </div>

          <label for="language">{{ t('language') }}</label>
          <div class="system-data">
            <select id="language" v-model="language" name="language">
              <option value="en">English</option>
              <option value="ru">Русский</option>
            </select>
          </div>

          <Button
            class="system-submit"
            type="submit"
            :disabled="!isChanged(['login', 'pass']) && language === locale"
          >
            {{ t('save') }}
          </Button>
        </form>
      </fieldset>

      <Configuration :cmd="cmd" :loaded-method="loadedMethod" />

      <fieldset class="system-container">
        <legend>{{ t('links') }}</legend>

        <a :href="documentation" target="_blank">{{ t('documentation') }}</a>
        <div></div>

        <a v-if="locale === 'ru'" :href="support" target="_blank">{{ t('support') }}</a>
        <a v-else :href="`mailto: ${email}`">{{ t('support') }}:&nbsp;{{ email }}</a>
        <div></div>

        <a :href="website" target="_blank">{{ t('website') }}</a>
        <div></div>
      </fieldset>
    </div>
  </Layout>
</template>

<style scoped>
.system {
  columns: 2;
  column-gap: 12px;

  @media (max-width: 1320px) {
    columns: 2;
  }

  @media (max-width: 1024px) {
    columns: 1;
    max-width: 470px;
  }

  @media (max-width: 500px) {
    width: 100%;
  }
}

.system-container {
  display: grid;
  gap: 6px 24px;
  grid-template-columns: 45% 55%;
  align-items: center;
  justify-items: flex-start;
  page-break-inside: avoid;
  break-inside: avoid;
}

.system-container div {
  height: 33px;
  align-items: center;
  display: flex;
}

.system-container div:nth-child(odd),
.system-data {
  width: calc(100% - 24px);
  display: flex;
  justify-content: end;
}

.system-submit {
  margin-top: 14px;
}

.system-textButton {
  appearance: none;
  background: transparent;
  color: var(--text-color);
  text-decoration: underline;
}

.system-textButton:hover,
.system-textButton:focus{
  background: transparent;
  outline: none;
  box-shadow: none;
  color: var(--link-color);
}

.system-info * {
  width: fit-content;
}

.system-saveWrapper {
  display: flex;
  gap: 6px;
}

.system-save {
  width: 12px;
  height: 12px;
}

.system-uptime {
  margin-right: 4px;
}
</style>

<i18n>
{
  "en": {
    "title": "System",
    "device": "Device",
    "hostname": "Hostname",
    "serial_num": "Serial number",
    "uptime": "Uptime",
    "uptime_days": "- | {n} day | {n} days | {n} days",
    "uptime_hours": "less than an hour | {n} hour | {n} hours | {n} hours",
    "uptime_minutes": "minute | {n} minute | {n} minutes | {n} minutes",
    "interface": "Web interface",
    "language": "Language",
    "firmware_version": "Firmware version",
    "firmware_update": "Firmware update",
    "wirmware_update_info": "The device will reboot after the update",
    "choose_firmware": "Choose file",
    "firmware_update_processed": "Firmware update in progress",
    "reboot": "Reboot",
    "restart": "Reboot device",
    "links": "Links",
    "documentation": "Documentation",
    "support": "Support",
    "website": "Buy devices"
  },
  "ru": {
    "title": "Система",
    "device": "Устройство",
    "hostname": "Название хоста",
    "serial_num": "Серийный номер",
    "uptime": "Время работы",
    "uptime_days": "- | {n} день | {n} дня | {n} дней",
    "uptime_hours": "- | {n} час | {n} часа | {n} часов",
    "uptime_minutes": "минута | {n} минута | {n} минуты | {n} минут",
    "interface": "Веб-интерфейс",
    "language": "Язык",
    "firmware_version": "Версия ПО",
    "firmware_update": "Обновление ПО",
    "wirmware_update_info": "После обновления устройство будет перезагружено",
    "choose_firmware": "Выбрать файл",
    "firmware_update_processed": "Обновление ПО в процессе",
    "reboot": "Перезагрузка",
    "restart": "Перезагрузить",
    "links": "Ссылки",
    "documentation": "Документация",
    "support": "Техподдержка",
    "website": "Купить устройства"
  }
}
</i18n>
