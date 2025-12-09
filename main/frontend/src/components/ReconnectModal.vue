<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useUptime } from '@/common/uptime';
import Loader from '@/assets/loader.svg?component';

const { t } = useI18n();
const dialog = ref<HTMLDialogElement>();
const { isReconnecting } = useUptime();
const countdown = ref(10);
const timer = ref<any>();

const tryReconnect = () => {
  countdown.value = 10;
  startCountdown();
};

const startCountdown = () => {
  clearInterval(timer.value);
  timer.value = setInterval(() => {
    countdown.value--;
    if (countdown.value <= 0) {
      clearInterval(timer.value);
      tryReconnect();
    }
  }, 1000);
};

const toggleModal = () => {
  if (isReconnecting.value) {
    dialog?.value?.showModal();
  } else {
    dialog?.value?.close();
  }
};

onMounted(() => {
  startCountdown();
  toggleModal();
});

watch(isReconnecting, () => {
  toggleModal();
});

onBeforeUnmount(() => clearInterval(timer.value));
</script>

<template>
  <Teleport to="body">
    <dialog ref="dialog" class="reconnectModal">
      <div class="reconnectModal-loaderWrapper">
        <Loader class="reconnectModal-loader" fill="currentColor" />
        <div class="reconnectModal-counter">{{ countdown }}</div>
      </div>
      {{ t('reconnecting') }}
    </dialog>
  </Teleport>
</template>

<style scoped>
.reconnectModal {
  background: #fff;
  padding: 24px;
  color: var(--text-color);
  border-radius: var(--border-radius);
  border: 0;
  align-items: center;
  display: flex;
  gap: 6px;
  margin: auto 24px;
  font-family: -apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica Neue,Arial,Noto Sans,sans-serif,"Apple Color Emoji","Segoe UI Emoji",Segoe UI Symbol,"Noto Color Emoji";
  font-size: 16px;
  justify-self: center;
  box-shadow: 0 0 20px 4px rgba(0, 0, 0, 0.4);
  animation: appear 200ms forwards;
  outline: none;
}

.reconnectModal::backdrop {
  background: rgba(7, 7, 7, 0.8);
  animation: appear-overlay 200ms forwards;
}

.reconnectModal-loaderWrapper {
  position: relative;
  width: 36px;
  height: 36px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.reconnectModal-counter {
  font-weight: 900;
  line-height: 1em;
  height: 18px;
}

.reconnectModal-loader {
  position: absolute;
  min-width: 36px;
  height: 36px;
  display: block;
  animation: rotate 1s linear infinite;
}
</style>

<i18n>
{
  "en": {
    "reconnecting": "Connection to the device lost. Attempting to reconnect…"
  },
  "ru": {
    "reconnecting": "Связь с устройством потеряна. Попытка переподключения…"
  }
}
</i18n>
