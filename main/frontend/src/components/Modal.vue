<script setup lang="ts">
import { nextTick, onMounted, ref, useId, watch } from 'vue';

const props = defineProps<{ open: boolean }>();
const emit = defineEmits<{ close: [] }>();

const dialog = ref<HTMLDialogElement>();
const titleId = useId();

const sync = () => {
  const element = dialog.value;
  if (!element) {
    return;
  }
  if (props.open && !element.open) {
    element.showModal();
  } else if (!props.open && element.open) {
    element.close();
  }
};

onMounted(sync);
watch(() => props.open, sync, { flush: 'post' });

// Esc only asks: the parent decides whether the dialog closes. A cancel bubbling up from a control
// inside is not the dialog's own.
const onCancel = (event: Event) => {
  if ((event.target !== dialog.value) || (!event.cancelable)) {
    return;
  }
  event.preventDefault();
  emit('close');
};

// The browser can still close the dialog by itself: a repeated Esc with no user activation in
// between comes as a cancel that cannot be prevented. The parent is asked the same way, and the
// dialog is shown again if the parent keeps it open.
const onClose = async () => {
  if (!props.open) {
    return;
  }
  emit('close');
  await nextTick();
  sync();
};
</script>

<template>
  <Teleport to="body">
    <dialog
      ref="dialog"
      class="modal"
      :aria-labelledby="$slots.title ? titleId : undefined"
      @cancel="onCancel"
      @close="onClose"
    >
      <template v-if="open">
        <div v-if="$slots.title || $slots.actions" class="modal-header">
          <h2 v-if="$slots.title" :id="titleId" class="modal-title"><slot name="title" /></h2>
          <div v-if="$slots.actions" class="modal-actions"><slot name="actions" /></div>
        </div>
        <div class="modal-body"><slot /></div>
      </template>
    </dialog>
  </Teleport>
</template>

<style scoped>
/* `display` is set only on the open dialog: an author display value would override the UA's hiding
   of a closed one, and this one stays mounted while closed. */
.modal {
  width: min(560px, 100vw - 32px);
  max-width: none;
  max-height: calc(100dvh - 32px);
  padding: 0;
  overflow: hidden;
  color: var(--text-color);
  background: var(--bg-surface);
  border: 1px solid var(--border-color);
  border-radius: var(--r-lg);
  box-shadow: 0 0 20px 4px rgba(0, 0, 0, 0.4);
  outline: none;
}

.modal[open] {
  display: flex;
  flex-direction: column;
}

.modal::backdrop {
  background: rgba(7, 7, 7, 0.8);
}

.modal-header {
  display: flex;
  flex-wrap: wrap;
  flex-shrink: 0;
  align-items: center;
  gap: 8px 12px;
  padding: 14px 18px;
  border-bottom: 1px solid var(--border-color);
}

.modal-title {
  margin: 0;
  font-size: 14px;
  font-weight: 600;
  letter-spacing: 0.01em;
}

.modal-actions {
  display: flex;
  gap: 6px;
  margin-left: auto;
}

.modal-body {
  min-height: 0;
  padding: 16px 18px;
  overflow-y: auto;
  overscroll-behavior: contain;
}

/* The global .field grid needs ~356px, more than a phone-width modal body has: let both columns
   shrink. On wider screens the label column is wider than 140px anyway, so nothing changes there. */
.modal-body :deep(.field) {
  grid-template-columns: minmax(0, 1fr) minmax(0, 200px);
}
</style>
