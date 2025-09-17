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
  text-align: center;
  user-select: none;
  padding: 6px 12px;
  border-radius: var(--border-radius);
  transition: color .15s ease-in-out, background-color .15s ease-in-out;
  outline: 0;
  border: 0;
}

.button:not(:disabled) {
  cursor: pointer;
}

.button:disabled {
  opacity: .65;
}

.button-caption {
  text-overflow: ellipsis;
  overflow: hidden;
}

.button-primary {
  color: #fff;
  background-color: var(--primary-color);
}

.button-primary:focus:not(:disabled),
.button-primary:hover:not(:disabled) {
  background: var(--primary-color-hover);
}

.button-danger {
  background: var(--danger-color);
  color: #fff;
}

.button-danger:hover {
  background: var(--danger-color-hover);
}

.button-gray {
  background: var(--gray-color);
  color: #495057;
}

.button-gray:focus:not(:disabled),
.button-gray:hover:not(:disabled) {
  background: var(--gray-color-hover);
}

.button-outline {
  border: 1px solid var(--border-color);
  background: none;
  color: var(--text-color);
}

.button-outline:hover:not(:disabled) {
  border: 1px solid var(--border-color);
  background: #f8fafc;
}

.button-loader {
  top: calc(50% - 10px);
  width: 14px;
  height: 14px;
  margin-right: 4px;
  animation: rotate 1s linear infinite;
}
</style>
