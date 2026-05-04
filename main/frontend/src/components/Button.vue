<script setup lang="ts">
import Loader from '@/assets/loader.svg?component';

const props = defineProps<{ variant?: 'primary' | 'gray' | 'outline' | 'danger'; isLoading?: boolean }>();
const emit = defineEmits(['click']);
</script>

<template>
  <button
    v-bind="props"
    :class="['button', {
      'button-primary': variant === 'primary' || !variant,
      'button-outline': variant === 'outline',
      'button-danger': variant === 'danger',
      'button-gray': variant === 'gray'
    }]"
    @click="emit('click', $event)">
    <Loader v-if="isLoading" class="button-loader" fill="currentColor" />
    <span class="button-caption">
      <slot />
    </span>
  </button>
</template>

<style scoped>
.button {
  position: relative;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  height: 30px;
  padding: 0 12px;
  border-radius: var(--r-md);
  font-family: var(--font-ui);
  font-size: 13.3px; /* +0.8px for Roboto */
  font-weight: 500;
  line-height: 1;
  border: 1px solid var(--border-strong);
  background: var(--bg-surface);
  color: var(--text-color);
  cursor: pointer;
  transition: background 0.12s, border-color 0.12s, color 0.12s;
  user-select: none;
  outline: 0;
}

.button:not(:disabled) {
  cursor: pointer;
}

.button:disabled {
  opacity: .65;
  cursor: unset;
}

.button-caption {
  text-overflow: ellipsis;
  overflow: hidden;
}

.button-primary {
  background: var(--primary-color);
  border-color: var(--primary-color);
  color: #fff;
}

.button-primary:focus:not(:disabled),
.button-primary:hover:not(:disabled) {
  background: var(--primary-color-hover);
  border-color: var(--primary-color-hover);
}

.button-danger {
  color: var(--danger-color);
  border-color: color-mix(in oklch, var(--danger-color) 40%, var(--border-strong));
  background: var(--bg-surface);
}

.button-danger:hover:not(:disabled) {
  background: var(--danger-soft);
  border-color: var(--danger-color);
}

.button-gray {
  background: var(--bg-surface-subtle);
  border-color: var(--border-color);
  color: var(--text-secondary);
}

.button-gray:focus:not(:disabled),
.button-gray:hover:not(:disabled) {
  background: var(--bg-surface);
  border-color: var(--border-strong);
}

.button-outline {
  border: 1px solid var(--border-color);
  background: var(--bg-surface);
  color: var(--text-color);
}

.button-outline:hover:not(:disabled) {
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
}

.button-loader {
  top: calc(50% - 10px);
  width: 14px;
  height: 14px;
  margin-right: 4px;
  animation: rotate 1s linear infinite;
}
</style>
