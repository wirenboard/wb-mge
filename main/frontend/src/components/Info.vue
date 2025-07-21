<script setup lang="ts">
import InfoIcon from '@/assets/infoIcon.svg?component';

defineProps<{ text: string }>();
</script>

<template>
  <div class="info">
    <InfoIcon class="info-icon" />
    {{ text }}
  </div>
</template>

<style>
.info {
  grid-column: span 2 !important;
  color: var(--gray-text);
  font-size: 13px;
  margin-top: -6px;
  height: fit-content !important;
  justify-content: start !important;
  display: flex;
  align-items: center;
  gap: 4px;
  width: 100%;
}

.info-icon {
  width: 14px;
  height: 14px;
  color: var(--gray-text);
}
</style>
