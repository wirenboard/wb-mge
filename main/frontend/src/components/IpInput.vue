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
import { computed, reactive } from 'vue';

const emit = defineEmits(['update:modelValue']);
const model = defineModel<string>();
const value = reactive([
  model.value?.split('.')[0] || '',
  model.value?.split('.')[1] || '',
  model.value?.split('.')[2] || '',
  model.value?.split('.')[3] || ''],
);

const valueToIp = computed(() => {
  return value.join('.');
});

const handleChange = (ev: any) => {
  if (ev.code.includes('Key')) {
    ev.preventDefault();
  }

  if (ev.target?.value?.length === 3) {
    ev.target?.parentElement?.nextElementSibling?.querySelector('input')?.focus();
  } else if (!ev.target?.value && ev.key === 'Backspace') {
    ev.target?.parentElement?.previousElementSibling?.querySelector('input')?.focus();
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
