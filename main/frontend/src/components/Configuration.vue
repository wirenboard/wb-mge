<script lang="ts" setup>
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import { downloadFile } from '@/utils/downloadFile';

const { t } = useI18n();
const fileInput = ref<any>();
const { data, updateSettings } = useSettings();

defineProps<{ loadedMethod?: string; cmd: (command: string, confirmText?: string) => Promise<void> }>();

const downloadSettings = () => {
  const fileName = `wb-mge3-settings-${new Date().toISOString()}.json`;
  const formattedData = JSON.stringify(data.value, null, 2);
  const file = new File([formattedData], fileName, {
    type: 'application/json'
  });

  downloadFile(fileName, file);
};

const handleFileChange = () => {
  const reader = new FileReader();
  reader.onload = async () => {
    await updateSettings(JSON.parse(reader.result as string));
    fileInput.value!.value = null;
  };
  reader.readAsText(fileInput.value.files[0]);
};
</script>

<template>
  <fieldset>
    <legend>{{ t('configuration') }}</legend>

    <div>{{ t('export') }}</div>
    <div class="settingsActions-button">
      <Button @click="downloadSettings">
        {{ t('downloadSettings') }}
      </Button>
    </div>

    <div>{{ t('import') }}</div>
    <div class="settingsActions-button">
      <Button @click="fileInput.click()">{{ t('uploadSettings') }}</Button>
      <input ref="fileInput" class="settingsActions-input" type="file" required accept=".json" @change="handleFileChange" />
    </div>

    <div>{{ t('reset') }}</div>
    <div class="settingsActions-button">
      <Button type="button" variant="danger" :disabled="loadedMethod === 'set_default_settings'" @click="cmd('set_default_settings', t('factory_reset_confirm'))">
        {{ t('set_default_settings') }}
      </Button>
    </div>
  </fieldset>
</template>

<style scoped>
.settingsActions-input {
  display: none;
}

.settingsActions-button {
  width: calc(100% - 24px);
  display: flex;
  justify-content: end;
}
</style>

<i18n>
{
  "en": {
    "configuration": "Configuration",
    "export": "Export",
    "downloadSettings": "Download settings",
    "import": "Import",
    "uploadSettings": "Upload settings",
    "settingsUploaded": "Settings uploaded",
    "reset": "Reset",
    "set_default_settings": "Factory reset",
    "factory_reset_confirm": "Are you sure you want to do a factory reset?"
  },
  "ru": {
    "configuration": "Конфигурация",
    "export": "Экспорт",
    "downloadSettings": "Сохранить в файл",
    "import": "Импорт",
    "uploadSettings": "Загрузить из файла",
    "settingsUploaded": "Настройки импортированы",
    "reset": "Сброс к заводским",
    "set_default_settings": "Сбросить",
    "factory_reset_confirm": "Вы уверены, что хотите сделать сброс к заводским настройкам?"
  }
}
</i18n>
