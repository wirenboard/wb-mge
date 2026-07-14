<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useAlerts } from '@/common/alert';

const { t } = useI18n();
const { alerts } = useAlerts();
</script>

<template>
  <div class="alert-wrapper">
    <TransitionGroup appear>
      <div
        v-for="(alert, key) in alerts" :key="key"
        role="alert"
        class="alert"
        :class="{
          'alert-success': alert.type === 'success',
          'alert-error': alert.type === 'error',
          'alert-warning': alert.type === 'warning',
        }"
      >
        <i18n-t v-if="alert.interpolation" :keypath="alert.message">
          {{ t(alert.interpolation) }}
        </i18n-t>
        <template v-else>
          {{ t(alert.message) }}
        </template>
      </div>
    </TransitionGroup>
  </div>
</template>

<style scoped>
.alert-wrapper {
  display: flex;
  flex-direction: column;
  align-items: end;
  position: absolute;
  top: 12px;
  right: 12px;
}

.alert {
  display: block;
  margin-top: 6px;
  color: #fff;
  padding: 12px;
  border-radius: var(--r-md);
  box-shadow: var(--shadow-md);
  width: fit-content;
  /* Warnings carry a full sentence, unlike the one-line success/error toasts. */
  max-width: min(420px, calc(100vw - 24px));
}

.alert-success {
  background: var(--primary-color);
}

.alert-error {
  background: var(--danger-color);
}

/* Amber, with dark ink: the settings WERE saved, this is not a failure — but it must not read
   as a clean success either. */
.alert-warning {
  background: var(--warn);
  color: var(--text-color);
}
</style>
