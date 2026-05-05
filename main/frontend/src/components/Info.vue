<script setup lang="ts">
import InfoIcon from '@/assets/infoIcon.svg?component';
import WarnIcon from '@/assets/warnIcon.svg?component';

defineProps<{ text: string; severity?: 'info' | 'error' }>();
</script>

<template>
  <div class="info">
    <WarnIcon v-if="severity === 'error'" class="info-icon info-error" />
    <InfoIcon v-else class="info-icon" />
    <span :class="{'info-error': severity === 'error'}">{{ text }}</span>
  </div>
</template>

<style>
.info {
  color: var(--text-muted);
  font-size: 11px;
  margin-top: -6px;
  display: flex;
  align-items: center;
  gap: 4px;
  width: 100%;
  white-space: break-spaces;
}

.info-icon {
  width: 14px;
  height: 14px;
  color: var(--text-muted);
  flex-shrink: 0;
}

.info-error {
  color: var(--danger-color) !important;
}
</style>
