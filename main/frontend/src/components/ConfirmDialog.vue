<script setup lang="ts">
import { onBeforeUnmount } from 'vue';
import { useI18n } from 'vue-i18n';
import { useConfirm } from '@/common/confirm';
import Button from '@/components/Button.vue';
import Modal from '@/components/Modal.vue';

const { t } = useI18n();
const { confirmState, settleConfirm } = useConfirm();

// Every page mounts its own Layout. A question still open when the page goes away (browser Back)
// is answered "no" here, or the next page would inherit an open state with no dialog on screen.
onBeforeUnmount(() => settleConfirm(false));
</script>

<template>
  <Modal :open="confirmState.open" @close="settleConfirm(false)">
    <p class="confirmDialog-message">{{ confirmState.message }}</p>
    <div class="confirmDialog-actions">
      <Button type="button" variant="outline" autofocus @click="settleConfirm(false)">{{ t('cancel') }}</Button>
      <Button type="button" variant="danger" @click="settleConfirm(true)">{{ confirmState.confirmLabel }}</Button>
    </div>
  </Modal>
</template>

<style scoped>
.confirmDialog-message {
  margin: 0 0 20px;
  font-size: 14.8px; /* +0.8px for Roboto */
  line-height: 1.4;
}

.confirmDialog-actions {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
}
</style>

<i18n>
{
  "en": {
    "cancel": "Cancel"
  },
  "ru": {
    "cancel": "Отмена"
  },
  "kk": {
    "cancel": "Болдырмау"
  },
  "it": {
    "cancel": "Annulla"
  },
  "de": {
    "cancel": "Abbrechen"
  }
}
</i18n>
