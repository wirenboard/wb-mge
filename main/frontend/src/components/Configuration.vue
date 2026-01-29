<script lang="ts" setup>
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import type { Settings } from '@/common/types';
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
    await updateSettings(JSON.parse(reader.result as string) as Settings);
    fileInput.value!.value = null;
  };
  reader.readAsText(fileInput.value.files[0]);
};
</script>

<template>
  <fieldset class="configuration-container">
    <legend>{{ t('configuration') }}</legend>

    <div>{{ t('export') }}</div>
    <div class="configuration-button">
      <Button @click="downloadSettings">
        {{ t('downloadSettings') }}
      </Button>
    </div>

    <div>{{ t('import') }}</div>
    <div class="configuration-button">
      <Button @click="fileInput.click()">{{ t('uploadSettings') }}</Button>
      <input ref="fileInput" class="configuration-input" type="file" required accept=".json" @change="handleFileChange" />
    </div>

    <div>{{ t('reset') }}</div>
    <div class="configuration-button">
      <Button type="button" variant="danger" :disabled="loadedMethod === 'set_default_settings'" @click="cmd('set_default_settings', t('factory_reset_confirm'))">
        {{ t('set_default_settings') }}
      </Button>
    </div>
  </fieldset>
</template>

<style scoped>
.configuration-container {
  display: grid;
  gap: 6px 24px;
  grid-template-columns: 60fr 40fr;
  align-items: center;
  justify-items: flex-start;
  page-break-inside: avoid;
  break-inside: avoid;
}

.configuration-container div {
  height: 33px;
  display: flex;
  align-items: center;
}

.system-container div:nth-child(odd)  {
  width: 100%;
  display: flex;
  justify-content: end;
}

.configuration-input {
  display: none;
}

.configuration-button {
  width: 100%;
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
    "reset": "Сброс к заводским настройкам",
    "set_default_settings": "Сбросить",
    "factory_reset_confirm": "Вы уверены, что хотите сделать сброс к заводским настройкам?"
  },
  "kk": {
    "configuration": "Конфигурация",
    "export": "Экспорт",
    "downloadSettings": "Файлға сақтау",
    "import": "Импорт",
    "uploadSettings": "Файлдан жүктеу",
    "settingsUploaded": "Баптаулар жүктелді",
    "reset": "Қалпына келтіру",
    "set_default_settings": "Зауыттыққа қайтару",
    "factory_reset_confirm": "Зауыттық баптауларға қайтаруды қалайсыз ба?"
  },
  "it": {
    "configuration": "Configurazione",
    "export": "Esporta",
    "downloadSettings": "Scarica impostazioni",
    "import": "Importa",
    "uploadSettings": "Carica impostazioni",
    "settingsUploaded": "Impostazioni caricate",
    "reset": "Ripristina",
    "set_default_settings": "Ripristino di fabbrica",
    "factory_reset_confirm": "Sei sicuro di voler ripristinare le impostazioni di fabbrica?"
  },
  "de": {
    "configuration": "Konfiguration",
    "export": "Export",
    "downloadSettings": "Einstellungen herunterladen",
    "import": "Import",
    "uploadSettings": "Einstellungen hochladen",
    "settingsUploaded": "Einstellungen hochgeladen",
    "reset": "Zurücksetzen",
    "set_default_settings": "Werkseinstellungen",
    "factory_reset_confirm": "Möchten Sie wirklich auf Werkseinstellungen zurücksetzen?"
  }
}
</i18n>
