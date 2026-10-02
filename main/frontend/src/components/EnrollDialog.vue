<script setup lang="ts">
import { type Component, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import EnrollDone from '@/assets/enrollDone.svg?component';
import EnrollFailed from '@/assets/enrollFailed.svg?component';
import EnrollLift from '@/assets/enrollLift.svg?component';
import EnrollPlace from '@/assets/enrollPlace.svg?component';
import EnrollProcessing from '@/assets/enrollProcessing.svg?component';
import { fpErrorMessage, isFpEnrollActive } from '@/common/fpAcs';
import type { FpEnrollState, FpStatus, GrayImage } from '@/common/types';
import Button from '@/components/Button.vue';
import FingerprintImage from '@/components/FingerprintImage.vue';
import Modal from '@/components/Modal.vue';

interface StateView {
  prompt: string;
  art: Component;
  modifier: string;
}

const props = defineProps<{
  open: boolean;
  enroll: FpStatus['enroll'];
  userName: string | null; // null while the user is not known
  image: GrayImage | null; // a take of this very enrollment, never an older image
  busy: boolean;
}>();
const emit = defineEmits<{ cancel: []; close: [] }>();

const DASH = '—';

// The modifier colours the ring of the picture and the progress like the sensor's own ring: blue
// for "place", purple while processing, green for "lift" and once done, red after a failure.
const STATE_VIEWS: Record<FpEnrollState, StateView> = {
  idle: { prompt: 'enroll_idle', art: EnrollPlace, modifier: 'enrollDialog-stateIdle' },
  place: { prompt: 'enroll_place', art: EnrollPlace, modifier: 'enrollDialog-statePlace' },
  processing: { prompt: 'enroll_processing', art: EnrollProcessing, modifier: 'enrollDialog-stateProcessing' },
  lift: { prompt: 'enroll_lift', art: EnrollLift, modifier: 'enrollDialog-stateLift' },
  done: { prompt: 'enroll_done', art: EnrollDone, modifier: 'enrollDialog-stateDone' },
  error: { prompt: 'enroll_error', art: EnrollFailed, modifier: 'enrollDialog-stateError' },
  cancelled: { prompt: 'enroll_cancelled', art: EnrollFailed, modifier: 'enrollDialog-stateCancelled' },
};

const { t } = useI18n();

const active = computed(() => isFpEnrollActive(props.enroll.state));

// A state this page does not know yet is drawn as idle, with the state as sent for the prompt.
const known = computed(() => props.enroll.state in STATE_VIEWS);
const view = computed(() => (known.value ? STATE_VIEWS[props.enroll.state] : STATE_VIEWS.idle));
const prompt = computed(() => (
  known.value ? t(view.value.prompt, { slot: props.enroll.slot ?? DASH }) : props.enroll.state
));

// A line of its own under the prompt: the device's reason can be too long for the large type.
const reason = computed(() => (
  ((props.enroll.state === 'error') && props.enroll.error) ? t(fpErrorMessage(props.enroll.error)) : ''
));

// A running enrollment is cancelled, never left behind: until it is over, Esc does nothing.
const close = () => {
  if (!active.value) {
    emit('close');
  }
};
</script>

<template>
  <Modal :open="open" @close="close">
    <template #title>{{ t('enroll_title', { name: userName ?? DASH }) }}</template>
    <template #actions>
      <Button v-if="active" type="button" variant="danger" :disabled="busy" @click="emit('cancel')">
        {{ t('cancel') }}
      </Button>
      <Button v-else type="button" variant="outline" @click="close">{{ t('close') }}</Button>
    </template>

    <div class="enrollDialog-state" :class="view.modifier">
      <div class="enrollDialog-pictures">
        <component :is="view.art" class="enrollDialog-art" aria-hidden="true" />
        <FingerprintImage v-if="image" class="enrollDialog-image" v-bind="image" :label="t('enroll_image_label')" />
      </div>
      <div class="enrollDialog-message" role="status">
        <p class="enrollDialog-prompt">{{ prompt }}</p>
        <p v-if="reason" class="enrollDialog-reason">{{ reason }}</p>
      </div>
      <ol class="enrollDialog-steps" aria-hidden="true">
        <li
          v-for="n in enroll.steps"
          :key="n"
          class="enrollDialog-step"
          :class="{ 'enrollDialog-stepFilled': n <= enroll.step }"
        />
      </ol>
      <p v-if="active" class="enrollDialog-stepLabel">{{ t('enroll_step', { n: enroll.step, m: enroll.steps }) }}</p>
    </div>
  </Modal>
</template>

<style scoped>
.enrollDialog-state {
  --enrollDialog-ring: var(--text-muted);
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 8px 0;
  text-align: center;
}

.enrollDialog-statePlace {
  --enrollDialog-ring: var(--info);
}

/* The palette has no purple: this one exists only to match the sensor's processing colour. */
.enrollDialog-stateProcessing {
  --enrollDialog-ring: oklch(0.55 0.2 305);
}

.enrollDialog-stateLift,
.enrollDialog-stateDone {
  --enrollDialog-ring: var(--success);
}

.enrollDialog-stateError {
  --enrollDialog-ring: var(--danger-color);
}

.enrollDialog-pictures {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  align-items: center;
  gap: 24px;
}

.enrollDialog-art {
  width: 160px;
  height: 160px;
  color: var(--text-secondary);
}

.enrollDialog-art :deep(.enrollArt-ring),
.enrollDialog-art :deep(.enrollArt-mark) {
  stroke: var(--enrollDialog-ring);
}

.enrollDialog-image {
  width: 160px;
}

.enrollDialog-message {
  display: flex;
  flex-direction: column;
  gap: 6px;
}

.enrollDialog-prompt {
  margin: 0;
  font-size: 26px;
  font-weight: 700;
  line-height: 1.25;
}

.enrollDialog-stateError .enrollDialog-prompt {
  color: var(--danger-color);
}

.enrollDialog-reason {
  margin: 0;
  font-size: 14px;
  color: var(--text-secondary);
  overflow-wrap: anywhere;
}

/* The fp_error_* texts are written to follow a colon; here one starts a line. */
.enrollDialog-reason::first-letter {
  text-transform: uppercase;
}

.enrollDialog-steps {
  display: flex;
  gap: 8px;
  margin: 4px 0 0;
  padding: 0;
  list-style: none;
}

.enrollDialog-step {
  width: 12px;
  height: 12px;
  border: 2px solid var(--border-strong);
  border-radius: 50%;
}

.enrollDialog-stepFilled {
  background: var(--enrollDialog-ring);
  border-color: var(--enrollDialog-ring);
}

.enrollDialog-stepLabel {
  margin: -4px 0 0;
  font-size: 13px;
  color: var(--text-muted);
}
</style>

<i18n>
{
  "en": {
    "enroll_title": "Adding a finger: {name}",
    "enroll_idle": "Waiting for the sensor…",
    "enroll_place": "Place your finger",
    "enroll_lift": "Lift your finger",
    "enroll_processing": "Processing…",
    "enroll_done": "Finger added to slot {slot}",
    "enroll_error": "Enrollment failed",
    "enroll_cancelled": "Enrollment cancelled",
    "enroll_step": "Step {n} of {m}",
    "enroll_image_label": "Fingerprint taken during this enrollment",
    "cancel": "Cancel",
    "close": "Close"
  },
  "ru": {
    "enroll_title": "Запись пальца: {name}",
    "enroll_idle": "Ожидание датчика…",
    "enroll_place": "Приложите палец",
    "enroll_lift": "Уберите палец",
    "enroll_processing": "Обработка…",
    "enroll_done": "Палец записан в ячейку {slot}",
    "enroll_error": "Не удалось записать палец",
    "enroll_cancelled": "Запись отменена",
    "enroll_step": "Шаг {n} из {m}",
    "enroll_image_label": "Снимок отпечатка, сделанный при этой записи",
    "cancel": "Отменить",
    "close": "Закрыть"
  },
  "kk": {
    "enroll_title": "Саусақты жазу: {name}",
    "enroll_idle": "Датчикті күту…",
    "enroll_place": "Саусақты қойыңыз",
    "enroll_lift": "Саусақты алыңыз",
    "enroll_processing": "Өңделуде…",
    "enroll_done": "Саусақ {slot} ұяшығына жазылды",
    "enroll_error": "Саусақты жазу мүмкін болмады",
    "enroll_cancelled": "Жазу тоқтатылды",
    "enroll_step": "Қадам {n} / {m}",
    "enroll_image_label": "Осы жазу кезінде түсірілген саусақ ізі",
    "cancel": "Болдырмау",
    "close": "Жабу"
  },
  "it": {
    "enroll_title": "Registrazione del dito: {name}",
    "enroll_idle": "In attesa del sensore…",
    "enroll_place": "Appoggia il dito",
    "enroll_lift": "Solleva il dito",
    "enroll_processing": "Elaborazione…",
    "enroll_done": "Dito registrato nello slot {slot}",
    "enroll_error": "Registrazione non riuscita",
    "enroll_cancelled": "Registrazione annullata",
    "enroll_step": "Passo {n} di {m}",
    "enroll_image_label": "Impronta acquisita durante questa registrazione",
    "cancel": "Annulla",
    "close": "Chiudi"
  },
  "de": {
    "enroll_title": "Finger registrieren: {name}",
    "enroll_idle": "Warten auf den Sensor…",
    "enroll_place": "Legen Sie den Finger auf",
    "enroll_lift": "Heben Sie den Finger ab",
    "enroll_processing": "Verarbeitung…",
    "enroll_done": "Finger auf Platz {slot} registriert",
    "enroll_error": "Registrierung fehlgeschlagen",
    "enroll_cancelled": "Registrierung abgebrochen",
    "enroll_step": "Schritt {n} von {m}",
    "enroll_image_label": "Bei dieser Registrierung aufgenommener Fingerabdruck",
    "cancel": "Abbrechen",
    "close": "Schließen"
  }
}
</i18n>
