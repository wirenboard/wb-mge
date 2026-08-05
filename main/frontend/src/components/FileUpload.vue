<script lang="ts" setup>
import { ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import Button from '@/components/Button.vue';

defineProps<{ accept?: string; placeholder: string; uploadingPlaceholder: string; disabled?: boolean; isLoading: boolean }>();
defineEmits(['upload']);

const { t } = useI18n();
const destinationPath = ref('');
const fileInput = ref<any>();
const files = defineModel<any>();

const handleFileChange = () => {
  files.value = fileInput.value?.files;
};

watch(files, () => {
  if (!files.value?.length) {
    fileInput.value!.value = null;
  }
});

const cancelChoice = () => {
  files.value = null;
  fileInput.value!.value = null;
};
</script>

<template>
  <form class="fileUpload" autocomplete="off">
    <input ref="fileInput" class="fileUpload-input" type="file" required :accept="accept" @change="handleFileChange" />
    <Button v-if="!files?.length" type="button" @click="fileInput!.click()">{{ placeholder }}</Button>
    <div v-else class="fileUpload-selected">
      <span class="fileUpload-name mono" :title="files[0]?.name">{{ files[0]?.name }}</span>
      <div class="fileUpload-wrapper">
        <input v-show="false" v-model="destinationPath" type="text" name="destination_path" required>
        <Button type="button" variant="gray" :disabled="disabled" @click="cancelChoice">{{ t('cancel') }}</Button>
        <Button type="button" :is-loading="isLoading" :disabled="disabled" @click="$emit('upload')">{{ uploadingPlaceholder ? uploadingPlaceholder : t('upload') }}</Button>
      </div>
    </div>
  </form>
</template>

<style scoped>
.fileUpload-input {
  display: none;
}

.fileUpload-selected {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 6px;
}

.fileUpload-name {
  max-width: 200px;
  font-size: 12px;
  color: var(--text-secondary);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.fileUpload-wrapper {
  display: flex;
  gap: 6px;
  flex-wrap: wrap;
  justify-content: flex-end;
}
</style>

<i18n>
{
  "en": {
    "cancel": "Cancel choice",
    "upload": "Upload"
  },
  "ru": {
    "cancel": "Отменить",
    "upload": "Загрузить"
  },
  "kk": {
    "cancel": "Таңдауды болдырмау",
    "upload": "Жүктеу"
  },
  "it": {
    "cancel": "Annulla",
    "upload": "Carica"
  },
  "de": {
    "cancel": "Abbrechen",
    "upload": "Hochladen"
  }
}
</i18n>
