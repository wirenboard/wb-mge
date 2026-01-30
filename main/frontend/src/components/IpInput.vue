<script setup lang="ts">
import { ref, computed, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { onCustomValidation } from '@/utils/validation';

const { t } = useI18n();
const model = defineModel<string>();
const props = defineProps<{ name?: string; disabled?: boolean }>();
const showError = ref(false);

const ipPattern =
  /^(25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)(\.(25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)){3}$/;

const isValid = computed(() => ipPattern.test(model.value as string));

const onBlur = () => {
  showError.value = !isValid.value;
};

const onInput = (ev: Event) => {
  model.value = (ev.target as HTMLInputElement).value.replace(/[^0-9.]/g, '');
  onCustomValidation(ev, t('wrong_ip_pattern'));
};

watch(model, () => {
  if (showError.value && isValid.value) {
    showError.value = false;
  }
});
</script>

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

<style scoped>
.ipInput-invalid {
  border-color: var(--danger-color);
}
</style>

<i18n>
{
  "en": {
    "wrong_ip_pattern": "Incorrect format — IPv4 is required: four numbers from 0 to 255, separated by dots"
  },
  "ru": {
    "wrong_ip_pattern": "Некорректный формат — требуется IPv4: четыре числа от 0 до 255, разделённые точками"
  },
  "kk": {
    "wrong_ip_pattern": "Қате пішім — IPv4 қажет: нүктелермен бөлінген 0–255 аралығындағы төрт сан"
  },
  "it": {
    "wrong_ip_pattern": "Formato non corretto — è richiesto IPv4: quattro numeri da 0 a 255 separati da punti"
  },
  "de": {
    "wrong_ip_pattern": "Ungültiges Format — IPv4 erforderlich: vier Zahlen von 0 bis 255, durch Punkte getrennt"
  }
}
</i18n>
