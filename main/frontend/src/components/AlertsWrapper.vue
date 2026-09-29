<script setup lang="ts">
import { onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useAlerts } from '@/common/alert';

const { t } = useI18n();
const { alerts } = useAlerts();
const wrapper = ref<HTMLDivElement>();

const popoverWrapper = () => {
  const element = wrapper.value;
  return ((element !== undefined) && (typeof element.showPopover === 'function')) ? element : undefined;
};

// A modal <dialog> sits in the top layer with a dimmed backdrop over the whole document, so a toast
// raised while one is open would be drawn under it. The wrapper is a manual popover instead, and each
// new alert re-shows it: that moves it to the top of the top layer, above whatever modal opened last.
// The last alert leaving does not hide it here: that would cut its fade-out short (see hideIfEmpty).
watch(() => alerts.length, (count, previous) => {
  const element = popoverWrapper();
  if ((element === undefined) || (count === 0)) {
    return;
  }
  if (element.matches(':popover-open')) {
    if (count <= previous) {
      return;
    }
    element.hidePopover();
  }
  element.showPopover();
}, { flush: 'post' });

const hideIfEmpty = () => {
  const element = popoverWrapper();
  if ((element !== undefined) && (alerts.length === 0) && element.matches(':popover-open')) {
    element.hidePopover();
  }
};

// The alerts are global and the layout is rebuilt on every page, so a toast raised just before a
// navigation is already there when this wrapper mounts.
onMounted(() => {
  const element = popoverWrapper();
  if ((element !== undefined) && (alerts.length > 0)) {
    element.showPopover();
  }
});
</script>

<template>
  <div ref="wrapper" class="alert-wrapper" popover="manual">
    <TransitionGroup appear @after-leave="hideIfEmpty">
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
/* The margin, padding, border, background and overflow reset the UA's popover box. */
.alert-wrapper {
  display: flex;
  flex-direction: column;
  align-items: end;
  position: fixed;
  inset: 12px 12px auto auto;
  margin: 0;
  padding: 0;
  border: 0;
  background: transparent;
  overflow: visible;
}

/* The display above would otherwise override the UA's hiding of a closed popover. */
.alert-wrapper:not(:popover-open) {
  display: none;
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
