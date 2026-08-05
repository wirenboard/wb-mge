<script setup lang="ts">
defineProps<{ label: string }>();
</script>

<template>
  <div class="info-row">
    <div class="info-row-label">{{ label }}</div>
    <div class="info-row-value">
      <slot />
    </div>
    <div v-if="$slots.hint" class="info-row-hint">
      <slot name="hint" />
    </div>
  </div>
</template>

<style scoped>
.info-row {
  display: grid;
  grid-template-columns: minmax(140px, 1fr) auto;
  gap: 8px 16px;
  align-items: center;
  padding: 9px 0;
  border-bottom: 1px dashed var(--border-color);
  font-size: 13px;
}
.info-row:last-child { border-bottom: 0; }
.info-row-label { color: var(--text-secondary); }
.info-row-value { color: var(--text-color); text-align: right; min-width: 0; overflow-wrap: anywhere; }
.info-row-value :deep(.mono) { font-size: 12.5px; }
.info-row-hint {
  grid-column: 1 / -1;
  font-size: 11px;
  color: var(--text-muted);
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: -4px;
}
</style>
