<script lang="ts" setup>
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import { downloadFile } from '@/utils/downloadFile';

const { t } = useI18n();
const fileInput = ref<any>();
const { data, updateSettings } = await useSettings();

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
  <Button @click="downloadSettings">
    {{ t('downloadSettings') }}
  </Button>
  <Button @click="fileInput.click()">{{ t('uploadSettings') }}</Button>
  <input ref="fileInput" class="settingsActions-input" type="file" required accept=".json" @change="handleFileChange" />
</template>

<style scoped>
.settingsActions-input {
  display: none;
}
</style>

<i18n>
{
  "en": {
    "downloadSettings": "Download settings",
    "uploadSettings": "Upload settings",
    "settingsUploaded": "Settings uploaded"
  }
}
</i18n>
