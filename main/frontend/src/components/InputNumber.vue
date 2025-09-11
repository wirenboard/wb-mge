<template>
  <input
    v-model.number="model"
    :class="{
      'input-invalid': invalid
    }"
    type="number"
    v-bind="$attrs"
    @keydown="onKeydown" />
</template>

<script setup lang="ts">
import { useAttrs, watch } from 'vue';

const model = defineModel<number>();

const attrs = useAttrs();
const props = defineProps<{ float?: boolean; invalid?: boolean }>();

const onKeydown = (ev: KeyboardEvent) => {
  const allowedKeys = [
    'Backspace', 'Delete', 'ArrowLeft', 'ArrowRight', 'Tab', 'Enter'
  ];

  if (allowedKeys.includes(ev.key)) return;

  if (
    (ev.key === '-' && +(attrs?.min as string) <= 1) ||
    ev.key === 'e' ||
    (!props.float && ev.key === '.') ||
    !/^\d$/.test(ev.key)
  ) {
    ev.preventDefault();
  }

  if (attrs.max && (model.value as number) > +attrs.max) {
    model.value = +attrs.max;
  }
};

watch(model, () => {
  if (attrs.max && (model.value as number) > +attrs.max) {
    model.value = +attrs.max;
  }
});
</script>

<style scoped>
.input-invalid {
  border: 1px solid var(--danger-color);
}
</style>
