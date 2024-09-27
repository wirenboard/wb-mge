<template>
  <div class="ipInput">
    <div class="ipInput-wrapper">
      <input v-model="value[0]" type="text" :maxLength="3" @input="handleInput" @keydown="handleChange" />
    </div>
    <div class="ipInput-wrapper">
      <input v-model="value[1]" type="text" :maxLength="3" @input="handleInput" @keydown="handleChange" />
    </div>
    <div class="ipInput-wrapper">
      <input v-model="value[2]" type="text" :maxLength="3" @input="handleInput" @keydown="handleChange" />
    </div>
    <div class="ipInput-wrapper">
      <input v-model="value[3]" type="text" :maxLength="3" @input="handleInput" @keydown="handleChange" />
    </div>
  </div>
</template>

<script setup lang="ts">
import { computed, reactive, watch } from 'vue';

const emit = defineEmits(['update:modelValue']);
const model = defineModel<string>();
const value = reactive([
  model.value?.split('.')[0] || '',
  model.value?.split('.')[1] || '',
  model.value?.split('.')[2] || '',
  model.value?.split('.')[3] || ''],
);

// check is value was updated from parent
watch(model, () => {
  value[0] = model.value?.split('.')[0] || '';
  value[1] = model.value?.split('.')[1] || '';
  value[2] = model.value?.split('.')[2] || '';
  value[3] = model.value?.split('.')[3] || '';
});

const valueToIp = computed(() => value.join('.'));

const handleChange = (ev: any) => {
  if (['Tab', 'Alt'].includes(ev.code) || ev.shiftKey || ev.ctrlKey) {
    return;
  }

  // prevent input chars
  if (ev.code.includes('Key')) {
    ev.preventDefault();
    return;
  }

  const parent = ev.target?.parentElement;
  const prev = parent?.previousElementSibling?.querySelector('input');
  const next = parent?.nextElementSibling?.querySelector('input');
  if (ev.key.includes('Arrow')) {
    if (ev.key === 'ArrowLeft' && ev.target.selectionStart === 0) {
      prev?.focus();
    } else if (ev.key === 'ArrowRight' && ev.target.selectionStart === ev.target.value.length) {
      next?.focus();
      next?.setSelectionRange(0, 0);
    }
    return;
  }

  if (ev.target?.value?.length === 3 && ev.key !== 'Backspace') {
    next?.focus();
  } else if ((!ev.target?.value && ev.key === 'Backspace')) {
    prev?.focus();
  }
};

const handleInput = () => emit('update:modelValue', valueToIp);
</script>

<style scoped>
.ipInput {
  display: flex;
  gap: 14px;
  max-width: calc(100% - 6px);
}

.ipInput-wrapper {
  position: relative;
}

.ipInput-wrapper:not(:last-of-type)::after {
  content: '.';
  right: -12px;
  bottom: 6px;
  position: absolute;
}

.ipInput input {
  width: 100%;
  text-align: center;
  padding: 5px 2px;
}

.ipInput input::-webkit-outer-spin-button,
.ipInput input::-webkit-inner-spin-button {
  -webkit-appearance: none;
}
</style>
