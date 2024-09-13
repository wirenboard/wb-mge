<script lang="ts" setup>
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import Button from '@/components/Button.vue';

defineProps<{ accept?: string; placeholder: string; disabled?: boolean }>();
defineEmits(['upload']);

const { t } = useI18n();
const destinationPath = ref('');
const fileInput = ref<any>();
const files = defineModel<any>();

const handleFileChange = () => {
  files.value = fileInput.value?.files;
};

const cancelChoice = () => {
  files.value = null;
  fileInput.value!.value = null;
};
</script>

<template>
  <form class="fileUpload">
    <input ref="fileInput" class="fileUpload-input" type="file" required :accept="accept" @change="handleFileChange" />
    <Button v-if="!files?.length" type="button" @click="fileInput!.click()">{{ placeholder }}</Button>
    <div v-else>
      <div class="fileUpload-wrapper">
        <input v-show="false" v-model="destinationPath" type="text" name="destination_path" required>
        <Button type="button" variant="gray" :disabled="disabled" @click="cancelChoice">{{ t('cancel') }}</Button>
        <Button type="button" :disabled="disabled" @click="$emit('upload')">{{ t('upload') }}</Button>
      </div>
    </div>
  </form>
</template>

<style scoped>
.fileUpload {
  padding-top: 12px;
}

.fileUpload-input {
  display: none;
}

.fileUpload-wrapper {
  display: flex;
  gap: 12px;
}
</style>

<i18n>
{
  "en": {
    "cancel": "Cancel choice",
    "upload": "Upload"
  }
}
</i18n>
