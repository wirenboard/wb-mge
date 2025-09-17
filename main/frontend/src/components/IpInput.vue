<template>
  <input
    v-model="model"
    class="ipInput"
    :class="{ 'ipInput-invalid': showError }"
    type="text"
    pattern="^(25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9])(\.(25[0-5]|2[0-4][0-9]|1[0-9]{2}|[1-9]?[0-9])){3}$"
    maxlength="15"
    required
    v-bind="props"
    @input="onInput"
    @blur="onBlur"
  />
</template>

<script setup lang="ts">
import { ref, computed, watch } from 'vue';

const model = defineModel<string>();
const props = defineProps<{ name?: string; disabled?: boolean }>();
const showError = ref(false);

const ipPattern =
  /^(25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)){3}$/;

const isValid = computed(() => ipPattern.test(model.value as string));

const onBlur = () => {
  showError.value = !isValid.value;
};

const onInput = () => {
  model.value = (model.value as string).replace(/[^0-9.]/g, '');
};

watch(model, () => {
  if (showError.value && isValid.value) {
    showError.value = false;
  }
});
</script>

<style scoped>
.ipInput-invalid {
  border-color: var(--danger-color);
}
</style>
