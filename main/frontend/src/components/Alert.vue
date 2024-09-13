<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { watch } from 'vue';
import { alertData } from '@/common/global';

const { t } =useI18n();

watch(
  alertData,
  () => {
    if (!alertData.value) {
      return;
    }

    if (alertData.value.type === 'error') {
      console.error(alertData.value.message);
    }
    const timeout = setTimeout(() => {
      alertData.value = null;
      clearTimeout(timeout);
    }, 3000);
  }
);
</script>

<template>
  <Transition>
    <div
      v-if="alertData?.message"
      role="alert"
      class="alert"
      :class="{
        'alert-success': alertData.type === 'success',
        'alert-error': alertData.type === 'error',
      }"
    >
      {{ alertData.withTranslation ? t(alertData?.message) : alertData?.message }}
    </div>
  </Transition>
</template>

<style scoped>
.alert {
  position: absolute;
  color: #fff;
  bottom: 12px;
  right: 12px;
  padding: 12px;
  border-radius: var(--border-radius);
}

.alert-success {
  background: var(--primary-color);
}

.alert-error {
  background: var(--danger-color);
}
</style>
