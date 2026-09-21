<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useInfo } from '@/common/info';
import {
  MIXER_MAX_DURATION_S,
  MIXER_MAX_SPEED_RPM,
  MIXER_MAX_STEPS,
  MIXER_MIN_MOTION_SPEED_RPM,
  MIXER_SPEED_STEP_RPM,
  MIXER_STEP_FORWARD,
  MIXER_STEP_PAUSE,
  MIXER_STEP_REVERSE,
  type MixerStep,
  clampStepSpeed,
  combineDuration,
  decodeProgram,
  effectiveFrequencyDhz,
  effectiveSpeedRpm,
  encodeProgram,
  splitDuration,
} from '@/common/mixerProgram';
import { useSettings } from '@/common/settings';
import type { DeepPartial, MixerState, Settings } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import InfoRow from '@/components/InfoRow.vue';
import InputNumber from '@/components/InputNumber.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';

// The live card counts down a running step, so it needs /info once a second — five times more
// often than the application-wide poller in App.vue, and only while this page is open.
const INFO_POLL_MS = 1000;
const PROGRAM_INDEXES = [0, 1, 2, 3];
const HARDWARE_KEYS = ['mix_rpm_50hz', 'mix_vfd_port', 'mix_vfd_addr', 'mix_rev_code', 'mix_dir_inv'];
const VFD_PORTS = [1, 2];
const REVERSE_CODES = [3, 5];
const MAX_DURATION_MINUTES = Math.floor(MIXER_MAX_DURATION_S / 60);

const STATE_TEXT: Record<MixerState, string> = {
  stopped: 'state_stopped',
  running: 'state_running',
  error: 'state_error',
};

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
const { info, fetchInfo } = useInfo();

let pollId: ReturnType<typeof setInterval> | null = null;

onMounted(() => {
  pollId = setInterval(() => {
    // A dropped poll needs no alert of its own: the next tick a second later either brings the
    // state back or the application-wide reconnect banner takes over.
    fetchInfo('low').catch(() => {});
  }, INFO_POLL_MS);
});

onUnmounted(() => {
  if (pollId !== null) {
    clearInterval(pollId);
    pollId = null;
  }
});

const mixer = computed(() => info.value?.mixer);

// The mixer object is in /info only in a firmware built with the engine — the Modbus master role
// — so its absence is what says this build has no mixer, the same way SerialPorts.vue tells a
// Modbus-slave build by the presence of its two settings keys. Until /info has been answered at
// all nothing is known yet, and the cards stay up with their dashes rather than flashing a
// "there is no mixer here" at every reload.
const mixerUnavailable = computed(() => info.value !== undefined && info.value.mixer === undefined);

const programKey = (index: number) => `mix_p${index}` as keyof Settings;
const programNameKey = (index: number) => `mix_p${index}_name` as keyof Settings;
const savedProgram = (index: number) => (data.value?.[programKey(index)] as string | undefined) ?? '';
const savedName = (index: number) => (data.value?.[programNameKey(index)] as string | undefined) ?? '';

interface ProgramDraft {
  name: string;
  steps: MixerStep[];
  corrupt: boolean;
}

const makeDraft = (index: number): ProgramDraft => {
  try {
    return { name: savedName(index), steps: decodeProgram(savedProgram(index)), corrupt: false };
  } catch (err) {
    // Neither the firmware nor this editor can produce such a string, so the only honest thing to
    // show is an empty list plus a warning — silently posting that empty list back would destroy
    // a program the device may still be running.
    return { name: savedName(index), steps: [], corrupt: true };
  }
};

const drafts = ref<ProgramDraft[]>(PROGRAM_INDEXES.map((index) => makeDraft(index)));
const editedIndex = ref(data.value?.mix_prog ?? 0);
const draft = computed(() => drafts.value[editedIndex.value]);

// Settings arrive before the page is mounted and are re-read after every save; either way the
// draft of a program whose stored value changed is rebuilt, while drafts of the other three keep
// whatever the user has typed into them.
watch(
  () => PROGRAM_INDEXES.map((index) => `${savedProgram(index)}\n${savedName(index)}`),
  (next, prev) => {
    PROGRAM_INDEXES.forEach((index) => {
      if (!prev || next[index] !== prev[index]) {
        drafts.value[index] = makeDraft(index);
      }
    });
  }
);

const programLabel = (index: number) => savedName(index) || t('program_untitled', { n: index + 1 });

const normalizeStep = (step: MixerStep): MixerStep => ({
  type: step.type,
  speed: clampStepSpeed(step.type, step.speed),
  duration: step.duration,
});

const normalizeInPlace = (step: MixerStep) => {
  Object.assign(step, normalizeStep(step));
};

// What the device will actually do with the typed speed. The firmware converts it into a drive
// frequency through mix_rpm_50hz and then clamps that into the 1.0 .. 99.0 Hz the drive can
// produce, so a speed below the floor comes out as a shaft that turns anyway — which has to be
// visible here rather than discovered on the machine.
const ratio50hz = computed(() => data.value?.mix_rpm_50hz ?? 0);

const stepFrequencyText = (step: MixerStep) =>
  (effectiveFrequencyDhz(clampStepSpeed(step.type, step.speed), ratio50hz.value) / 10).toFixed(1);

const stepEffectiveRpm = (step: MixerStep) =>
  effectiveSpeedRpm(clampStepSpeed(step.type, step.speed), ratio50hz.value);

// Encoding the normalized copy rather than the draft itself: a half-typed speed is snapped onto
// the 10 rpm grid here, so neither this comparison nor the Save button can be blocked by a value
// the user has not finished entering.
const draftEncoded = computed(() => encodeProgram(draft.value.steps.map(normalizeStep)));

const savedEncoded = (index: number): string | null => {
  try {
    return encodeProgram(decodeProgram(savedProgram(index)));
  } catch (err) {
    return null;
  }
};

const isDraftChanged = computed(() => {
  const index = editedIndex.value;
  if (draft.value.name !== savedName(index)) return true;
  // A stored program that cannot be read is always "changed": overwriting it is the only way out.
  const saved = savedEncoded(index);
  return saved === null || saved !== draftEncoded.value;
});

const addStep = () => {
  if (draft.value.steps.length >= MIXER_MAX_STEPS) return;
  draft.value.steps.push({ type: MIXER_STEP_FORWARD, speed: 200, duration: 60 });
};

const removeStep = (index: number) => {
  draft.value.steps.splice(index, 1);
};

const moveStep = (index: number, offset: number) => {
  const target = index + offset;
  const steps = draft.value.steps;
  if (target < 0 || target >= steps.length) return;
  [steps[index], steps[target]] = [steps[target], steps[index]];
};

const setDurationMinutes = (step: MixerStep, value?: number) => {
  step.duration = combineDuration(value ?? 0, splitDuration(step.duration).seconds);
};

const setDurationSeconds = (step: MixerStep, value?: number) => {
  step.duration = combineDuration(splitDuration(step.duration).minutes, value ?? 0);
};

// The master switch and the program selector are acted on at once rather than collected for a
// Save: they are the two controls that mean "do it now", and a mixer left running because a
// button was never pressed is the failure worth avoiding. Each posts only its own key, so the
// two cannot overwrite one another when both are touched in quick succession.
// Switch models a `boolean | undefined`, which the checkbox it wraps can never actually emit;
// the fallback is what keeps the posted value a boolean rather than a missing key.
const applyEnabled = (value?: boolean) => {
  const enabled = value ?? false;
  data.value!.mix_enabled = enabled;
  updateSettings({ mix_enabled: enabled });
};

const applyProgram = () => {
  updateSettings({ mix_prog: data.value!.mix_prog });
};

const saveHardware = () => {
  updateSettings({
    mix_rpm_50hz: data.value!.mix_rpm_50hz,
    mix_vfd_port: data.value!.mix_vfd_port,
    mix_vfd_addr: data.value!.mix_vfd_addr,
    mix_rev_code: data.value!.mix_rev_code,
    mix_dir_inv: data.value!.mix_dir_inv,
  });
};

// Only this program's two keys go out: the whole settings document does not fit the firmware's
// 4 KB request body.
const saveProgram = () => {
  const index = editedIndex.value;
  updateSettings({
    [programKey(index)]: draftEncoded.value,
    [programNameKey(index)]: draft.value.name,
  } as DeepPartial<Settings>);
};

const stateText = computed(() => {
  const state = mixer.value?.state;
  return state && STATE_TEXT[state] ? t(STATE_TEXT[state]) : '—';
});

const statePillClass = computed(() => {
  switch (mixer.value?.state) {
    case 'running':
      return 'ok';
    case 'error':
      return 'mixer-pillError';
    default:
      return 'muted';
  }
});

const durationText = (seconds?: number) => {
  if (typeof seconds !== 'number' || !Number.isFinite(seconds)) return '—';
  const total = Math.max(Math.round(seconds), 0);
  const minutes = Math.floor(total / 60);
  return minutes > 0
    ? t('duration_min_sec', { m: minutes, s: total % 60 })
    : t('duration_sec', { s: total });
};

const measure = (value?: number, digits = 1) =>
  (typeof value === 'number' && Number.isFinite(value) ? value.toFixed(digits) : '—');

// The step the device reports is an index into the running program, which is how its description
// is looked up; the number the user sees is that index plus one.
const runningProgram = computed<MixerStep[]>(() => {
  const index = mixer.value?.program ?? data.value?.mix_prog ?? 0;
  try {
    return decodeProgram(savedProgram(index));
  } catch (err) {
    return [];
  }
});

const runningStep = computed<MixerStep | undefined>(() => runningProgram.value[mixer.value?.step ?? -1]);

const runningStepText = computed(() => {
  const step = runningStep.value;
  if (!step) return '';
  if (step.type === MIXER_STEP_PAUSE) return t('step_type_pause');
  const type = step.type === MIXER_STEP_REVERSE ? t('step_type_reverse') : t('step_type_forward');
  return t('step_summary', { type, speed: step.speed, duration: durationText(step.duration) });
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <!-- A build without the engine has nothing to show and nothing worth editing: its /info
           carries no mixer, and its firmware reads none of the mix_* settings. -->
      <section v-if="mixerUnavailable" id="mixer_absent" class="card">
        <div class="card-header">
          <div class="card-title-wrap">
            <div class="title">{{ t('no_mixer') }}</div>
            <div class="sub">{{ t('no_mixer_sub') }}</div>
          </div>
        </div>
      </section>

      <div v-if="!mixerUnavailable" class="grid-2 mixer-grid">
        <div class="stack">
          <section class="card">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">{{ t('control') }}</div>
                <div class="sub">{{ t('control_sub') }}</div>
              </div>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="mix_enabled">{{ t('mix_enabled') }}</label>
                <div class="field-switch">
                  <Switch
                    id="mix_enabled"
                    :model-value="data!.mix_enabled"
                    @update:model-value="applyEnabled"
                  />
                </div>
              </div>
              <div class="field">
                <label for="mix_prog">{{ t('mix_prog') }}</label>
                <!-- v-model assigns before this listener runs, which is what lets the handler read
                     the new value off the settings object rather than off the event. -->
                <select id="mix_prog" v-model="data!.mix_prog" name="mix_prog" @change="applyProgram">
                  <option v-for="index in PROGRAM_INDEXES" :key="`run_${index}`" :value="index">{{ programLabel(index) }}</option>
                </select>
              </div>
              <InfoRow :label="t('state')">
                <span class="pill" :class="statePillClass"><span class="dot" />{{ stateText }}</span>
              </InfoRow>
              <InfoRow :label="t('current_step')">
                <span v-if="mixer && runningProgram.length" id="mixer_step">
                  {{ t('step_position', { index: mixer.step + 1, total: runningProgram.length }) }}
                  <template v-if="runningStepText"> · {{ runningStepText }}</template>
                </span>
                <span v-else class="muted">—</span>
              </InfoRow>
              <InfoRow :label="t('step_left')">
                <span id="mixer_step_left" class="mono">{{ mixer ? durationText(mixer.step_left) : '—' }}</span>
              </InfoRow>
              <InfoRow :label="t('speed')">
                <span id="mixer_speed" class="mono">{{ mixer ? t('rpm_value', { n: Math.round(mixer.speed) }) : '—' }}</span>
              </InfoRow>
              <InfoRow :label="t('direction')">
                <!-- A pause step has no direction at all, so only the two motion types name one. -->
                <span id="mixer_direction">{{ mixer && mixer.step_type !== MIXER_STEP_PAUSE
                  ? t(mixer.step_type === MIXER_STEP_REVERSE ? 'step_type_reverse' : 'step_type_forward')
                  : '—' }}</span>
              </InfoRow>
            </div>
          </section>

          <section class="card">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">{{ t('vfd') }}</div>
                <div class="sub">{{ t('vfd_sub') }}</div>
              </div>
              <span v-if="mixer?.vfd_online" class="pill ok"><span class="dot" />{{ t('vfd_online') }}</span>
              <span v-else class="pill muted">{{ t('vfd_offline') }}</span>
            </div>
            <div class="card-body">
              <InfoRow :label="t('frequency')">
                <span id="mixer_frequency" class="mono">{{ measure(mixer?.frequency) }} {{ t('hz') }}</span>
              </InfoRow>
              <InfoRow :label="t('current')">
                <span id="mixer_current" class="mono">{{ measure(mixer?.current, 2) }} {{ t('a') }}</span>
              </InfoRow>
              <InfoRow :label="t('voltage')">
                <span id="mixer_voltage" class="mono">{{ measure(mixer?.voltage, 0) }} {{ t('v') }}</span>
              </InfoRow>
              <InfoRow :label="t('temperature')">
                <span id="mixer_temperature" class="mono">{{ measure(mixer?.temperature, 0) }} {{ t('celsius') }}</span>
              </InfoRow>
            </div>
          </section>

          <section class="card">
            <form @submit.prevent="saveHardware">
              <div class="card-header">
                <div class="card-title-wrap">
                  <div class="title">{{ t('hardware') }}</div>
                  <div class="sub">{{ t('hardware_sub') }}</div>
                </div>
                <Button
                  type="submit"
                  :is-loading="isLoading && isChanged(HARDWARE_KEYS)"
                  :disabled="isLoading || !isChanged(HARDWARE_KEYS)"
                >
                  {{ t('save') }}
                </Button>
              </div>
              <div class="card-body">
                <div class="field">
                  <label for="mix_rpm_50hz">{{ t('mix_rpm_50hz') }}</label>
                  <InputNumber id="mix_rpm_50hz" v-model="data!.mix_rpm_50hz" class="mono" name="mix_rpm_50hz" min="1" max="20000" required />
                  <div class="field-hint">{{ t('mix_rpm_50hz_hint') }}</div>
                </div>
                <div class="field">
                  <label for="mix_vfd_port">{{ t('mix_vfd_port') }}</label>
                  <select id="mix_vfd_port" v-model="data!.mix_vfd_port" name="mix_vfd_port">
                    <option v-for="port in VFD_PORTS" :key="`port_${port}`" :value="port">{{ t('port_n', { n: port }) }}</option>
                  </select>
                </div>
                <div class="field">
                  <label for="mix_vfd_addr">{{ t('mix_vfd_addr') }}</label>
                  <InputNumber id="mix_vfd_addr" v-model="data!.mix_vfd_addr" class="mono" name="mix_vfd_addr" min="1" max="247" required />
                </div>
                <div class="field">
                  <label for="mix_rev_code">{{ t('mix_rev_code') }}</label>
                  <select id="mix_rev_code" v-model="data!.mix_rev_code" name="mix_rev_code">
                    <option v-for="code in REVERSE_CODES" :key="`rev_${code}`" :value="code">{{ code }}</option>
                  </select>
                  <div class="field-hint">{{ t('mix_rev_code_hint') }}</div>
                </div>
                <div class="field">
                  <label for="mix_dir_inv">{{ t('mix_dir_inv') }}</label>
                  <div class="field-switch"><Switch id="mix_dir_inv" v-model="data!.mix_dir_inv" /></div>
                  <div class="field-hint">{{ t('mix_dir_inv_hint') }}</div>
                </div>
              </div>
            </form>
          </section>
        </div>

        <section class="card">
          <form @submit.prevent="saveProgram">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">{{ t('editor') }}</div>
                <div class="sub">{{ t('editor_sub', { max: MIXER_MAX_STEPS }) }}</div>
              </div>
              <Button
                id="mix_save_program"
                type="submit"
                :is-loading="isLoading && isDraftChanged"
                :disabled="isLoading || !isDraftChanged"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="mix_edited">{{ t('edited_program') }}</label>
                <select id="mix_edited" v-model="editedIndex" name="mix_edited">
                  <option v-for="index in PROGRAM_INDEXES" :key="`edit_${index}`" :value="index">{{ programLabel(index) }}</option>
                </select>
              </div>
              <div class="field">
                <label for="mix_name">{{ t('program_name') }}</label>
                <!-- The device refuses a nameless program. Its setting value holds 63 BYTES; the
                     31 here is the room that leaves for a name written in two-byte Cyrillic, which
                     is the shortest a name of 31 characters can be stored in. -->
                <input id="mix_name" v-model="draft.name" type="text" name="mix_name" maxlength="31" required>
              </div>

              <div v-if="draft.corrupt" id="mix_corrupt" class="mixer-warning">{{ t('program_corrupt') }}</div>

              <div class="mixer-steps">
                <div v-if="!draft.steps.length" class="mixer-empty muted">{{ t('no_steps') }}</div>
                <div v-for="(step, index) in draft.steps" :key="`step_${index}`" class="mixer-step">
                  <div class="mixer-step-number mono">{{ index + 1 }}</div>
                  <div class="mixer-step-field mixer-step-field--type">
                    <label :for="`mix_step_${index}_type`">{{ t('step_type') }}</label>
                    <!-- Switching to a step that moves the shaft snaps its speed onto the storable
                         grid at once, so the field never shows a value the device would not take. -->
                    <select
                      :id="`mix_step_${index}_type`"
                      v-model="step.type"
                      :name="`mix_step_${index}_type`"
                      @change="normalizeInPlace(step)"
                    >
                      <option :value="MIXER_STEP_PAUSE">{{ t('step_type_pause') }}</option>
                      <option :value="MIXER_STEP_FORWARD">{{ t('step_type_forward') }}</option>
                      <option :value="MIXER_STEP_REVERSE">{{ t('step_type_reverse') }}</option>
                    </select>
                  </div>
                  <div class="mixer-step-field">
                    <label :for="`mix_step_${index}_speed`">{{ t('step_speed') }}</label>
                    <!-- A step that turns the shaft may not ask for zero: the device would raise it
                         to the drive's slowest frequency and turn the motor. A pause is what
                         standing still is for, so only a pause starts at zero. -->
                    <InputNumber
                      :id="`mix_step_${index}_speed`"
                      v-model="step.speed"
                      class="mono"
                      :disabled="step.type === MIXER_STEP_PAUSE"
                      :min="step.type === MIXER_STEP_PAUSE ? 0 : MIXER_MIN_MOTION_SPEED_RPM"
                      :max="MIXER_MAX_SPEED_RPM"
                      :step="MIXER_SPEED_STEP_RPM"
                      @change="normalizeInPlace(step)"
                    />
                    <!-- What that speed actually becomes after the conversion through mix_rpm_50hz
                         and the drive's own 1.0 .. 99.0 Hz limits. -->
                    <div v-if="step.type !== MIXER_STEP_PAUSE" :id="`mix_step_${index}_effect`" class="mixer-step-effect mono">
                      {{ t('step_effective', { hz: stepFrequencyText(step), rpm: stepEffectiveRpm(step) }) }}
                    </div>
                  </div>
                  <div class="mixer-step-field">
                    <label :for="`mix_step_${index}_min`">{{ t('step_minutes') }}</label>
                    <InputNumber
                      :id="`mix_step_${index}_min`"
                      class="mono"
                      :model-value="splitDuration(step.duration).minutes"
                      min="0"
                      :max="MAX_DURATION_MINUTES"
                      @update:model-value="(value?: number) => setDurationMinutes(step, value)"
                    />
                  </div>
                  <div class="mixer-step-field">
                    <label :for="`mix_step_${index}_sec`">{{ t('step_seconds') }}</label>
                    <InputNumber
                      :id="`mix_step_${index}_sec`"
                      class="mono"
                      :model-value="splitDuration(step.duration).seconds"
                      min="0"
                      max="59"
                      @update:model-value="(value?: number) => setDurationSeconds(step, value)"
                    />
                  </div>
                  <div class="mixer-step-actions">
                    <Button :id="`mix_step_${index}_up`" type="button" variant="outline" :disabled="index === 0" @click="moveStep(index, -1)">
                      {{ t('step_up') }}
                    </Button>
                    <Button :id="`mix_step_${index}_down`" type="button" variant="outline" :disabled="index === draft.steps.length - 1" @click="moveStep(index, 1)">
                      {{ t('step_down') }}
                    </Button>
                    <Button :id="`mix_step_${index}_remove`" type="button" variant="danger" @click="removeStep(index)">
                      {{ t('step_remove') }}
                    </Button>
                  </div>
                </div>
              </div>

              <div class="mixer-editor-footer">
                <Button id="mix_step_add" type="button" variant="gray" :disabled="draft.steps.length >= MIXER_MAX_STEPS" @click="addStep">
                  {{ t('add_step') }}
                </Button>
                <span class="muted">{{ t('steps_count', { n: draft.steps.length, max: MIXER_MAX_STEPS }) }}</span>
              </div>
            </div>
          </form>
        </section>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
/* Not the even halves .grid-2 gives by default: a step row of the editor needs about 840px before
   its fields wrap, so the editor is given everything the left column does not need. What the left
   column needs is 394px and not a pixel less - a .field is minmax(140px, 1fr) + 200px with a 16px
   gap, plus the card's padding and border - and a narrower track does not push the cards wider,
   it pushes their controls out through the right-hand edge. The one-column breakpoint is repeated
   from .grid-2 because this rule would otherwise outlive it. */
.mixer-grid {
  grid-template-columns: minmax(400px, 440px) 1fr;
}

@media (max-width: 1024px) {
  .mixer-grid {
    grid-template-columns: 1fr;
  }
}

.field-switch {
  justify-self: end;
}

.field-hint {
  grid-column: 1 / -1;
  font-size: 11px;
  color: var(--text-muted);
  margin-top: -10px;
  line-height: 1.4;
}

.mixer-pillError {
  color: var(--danger-color);
  border-color: color-mix(in oklch, var(--danger-color) 40%, var(--border-color));
  background: var(--danger-soft);
}

.mixer-pillError .dot {
  background: var(--danger-color);
}

.mixer-warning {
  font-size: 12px;
  color: var(--warn);
  padding: 6px 0;
}

.mixer-steps {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 8px 0;
}

.mixer-empty {
  padding: 8px 0;
}

.mixer-step {
  display: flex;
  align-items: flex-end;
  flex-wrap: wrap;
  gap: 10px;
  padding: 8px 10px;
  border: 1px solid var(--border-color);
  border-radius: var(--r-md);
  background: var(--bg-surface-subtle);
}

.mixer-step-number {
  min-width: 18px;
  padding-bottom: 25px;
  color: var(--text-muted);
}

/* A fixed width rather than a content-driven one: the effective-value line below the speed is
   wider than the number above it and differs from step to step, so a column that sized itself to
   its contents put every row's inputs at a different x. */
.mixer-step-field {
  position: relative;
  display: flex;
  flex-direction: column;
  gap: 3px;
  width: 118px;
  flex: 0 0 auto;
  /* The band the effective-value line hangs in. Reserved by EVERY field, including the ones that
     never show such a line, because the row is bottom-aligned: a band on the speed field alone
     would lift its input above the others, and a row without one would be shorter than the rest. */
  margin-bottom: 18px;
}

.mixer-step-field--type {
  width: 132px;
}

/* Out of the flow, so it changes neither the width of its column nor the height of its row. */
.mixer-step-effect {
  position: absolute;
  top: 100%;
  left: 0;
  margin-top: 3px;
  white-space: nowrap;
  font-size: 10.5px;
  color: var(--text-muted);
}

.mixer-step-field > label {
  font-size: 10.5px;
  text-transform: uppercase;
  letter-spacing: 0.06em;
  color: var(--text-muted);
}

.mixer-step-actions {
  display: flex;
  gap: 6px;
  margin-left: auto;
  /* The same reserved band, so the buttons stay on the line of the inputs and not below it. */
  margin-bottom: 18px;
}

.mixer-editor-footer {
  display: flex;
  align-items: center;
  gap: 12px;
  padding-top: 6px;
}
</style>

<i18n>
{
  "en": {
    "title": "Mixer",
    "crumbs": "Slip mixer on a VFD",
    "no_mixer": "This firmware has no mixer",
    "no_mixer_sub": "The mixer engine is built into the Modbus master firmware only. This device reports no mixer, so there is nothing here to show and nothing to set.",
    "save": "Save",
    "control": "Mixer",
    "control_sub": "Master switch, the program being run and what it is doing right now.",
    "mix_enabled": "Mixer enabled",
    "mix_prog": "Active program",
    "state": "State",
    "state_stopped": "Stopped",
    "state_running": "Running",
    "state_error": "Error",
    "current_step": "Current step",
    "step_position": "Step {index} of {total}",
    "step_summary": "{type} · {speed} rpm · {duration}",
    "step_left": "Left in this step",
    "speed": "Set speed",
    "direction": "Direction",
    "rpm_value": "{n} rpm",
    "duration_min_sec": "{m} min {s} s",
    "duration_sec": "{s} s",
    "vfd": "Variable frequency drive",
    "vfd_sub": "What the drive reports over RS-485.",
    "vfd_online": "Link",
    "vfd_offline": "No link",
    "frequency": "Output frequency",
    "current": "Motor current",
    "voltage": "DC bus voltage",
    "temperature": "Heatsink temperature",
    "hz": "Hz",
    "a": "A",
    "v": "V",
    "celsius": "°C",
    "hardware": "Drive connection",
    "hardware_sub": "How the drive is wired and how its speed is converted.",
    "mix_rpm_50hz": "Shaft speed at 50 Hz",
    "mix_rpm_50hz_hint": "Shaft rpm the gearbox gives at 50 Hz. The requested speed is converted into a drive frequency with this number.",
    "mix_vfd_port": "RS-485 port",
    "mix_vfd_addr": "Modbus address of the drive",
    "mix_rev_code": "Reverse command code",
    "mix_rev_code_hint": "Command the drive expects for reverse: it depends on the drive revision.",
    "mix_dir_inv": "Invert direction",
    "mix_dir_inv_hint": "Swaps forward and reverse on the wire, so a program's step names match the way the shaft turns — for a motor wired the other way round.",
    "port_n": "Port {n}",
    "editor": "Program editor",
    "editor_sub": "Up to {max} steps: a pause, or a run forward or in reverse for a set time.",
    "edited_program": "Program",
    "program_name": "Name",
    "program_untitled": "Program {n}",
    "program_corrupt": "The stored program could not be read and is shown empty. Saving will overwrite it.",
    "no_steps": "The program is empty: the mixer does nothing.",
    "step_type": "Step",
    "step_type_pause": "Pause",
    "step_type_forward": "Forward",
    "step_type_reverse": "Reverse",
    "step_speed": "rpm",
    "step_effective": "{hz} Hz · {rpm} rpm",
    "step_minutes": "min",
    "step_seconds": "s",
    "step_up": "Up",
    "step_down": "Down",
    "step_remove": "Remove",
    "add_step": "Add step",
    "steps_count": "{n} of {max} steps"
  },
  "ru": {
    "title": "Мешалка",
    "crumbs": "Мешалка шликера на частотнике",
    "no_mixer": "Эта прошивка собрана без мешалки",
    "no_mixer_sub": "Движок мешалки есть только в прошивке для роли Modbus-мастера. Это устройство о мешалке не сообщает: показывать и настраивать здесь нечего.",
    "save": "Сохранить",
    "control": "Мешалка",
    "control_sub": "Общий выключатель, активная программа и то, что происходит прямо сейчас.",
    "mix_enabled": "Мешалка включена",
    "mix_prog": "Активная программа",
    "state": "Состояние",
    "state_stopped": "Остановлена",
    "state_running": "Работает",
    "state_error": "Ошибка",
    "current_step": "Текущий шаг",
    "step_position": "Шаг {index} из {total}",
    "step_summary": "{type} · {speed} об/мин · {duration}",
    "step_left": "До конца шага",
    "speed": "Заданные обороты",
    "direction": "Направление",
    "rpm_value": "{n} об/мин",
    "duration_min_sec": "{m} мин {s} с",
    "duration_sec": "{s} с",
    "vfd": "Частотный преобразователь",
    "vfd_sub": "Что частотник сообщает по RS-485.",
    "vfd_online": "Связь есть",
    "vfd_offline": "Нет связи",
    "frequency": "Выходная частота",
    "current": "Ток двигателя",
    "voltage": "Напряжение шины",
    "temperature": "Температура радиатора",
    "hz": "Гц",
    "a": "А",
    "v": "В",
    "celsius": "°C",
    "hardware": "Подключение частотника",
    "hardware_sub": "Как частотник подключён и как пересчитываются обороты.",
    "mix_rpm_50hz": "Обороты вала при 50 Гц",
    "mix_rpm_50hz_hint": "Сколько оборотов в минуту даёт вал на 50 Гц. По этому числу заданные обороты пересчитываются в частоту.",
    "mix_vfd_port": "Порт RS-485",
    "mix_vfd_addr": "Modbus-адрес частотника",
    "mix_rev_code": "Код команды реверса",
    "mix_rev_code_hint": "Какую команду частотник ждёт для реверса: зависит от его ревизии.",
    "mix_dir_inv": "Инвертировать направление",
    "mix_dir_inv_hint": "Меняет местами «вперёд» и «реверс» на шине: чтобы названия шагов в программе совпадали с тем, куда реально крутится вал, если двигатель подключён наоборот.",
    "port_n": "Порт {n}",
    "editor": "Редактор программы",
    "editor_sub": "До {max} шагов: пауза либо вращение вперёд или в реверсе заданное время.",
    "edited_program": "Программа",
    "program_name": "Название",
    "program_untitled": "Программа {n}",
    "program_corrupt": "Сохранённую программу не удалось прочитать, она показана пустой. Сохранение перезапишет её.",
    "no_steps": "Программа пуста: мешалка ничего не делает.",
    "step_type": "Шаг",
    "step_type_pause": "Пауза",
    "step_type_forward": "Вперёд",
    "step_type_reverse": "Реверс",
    "step_speed": "об/мин",
    "step_effective": "{hz} Гц · {rpm} об/мин",
    "step_minutes": "мин",
    "step_seconds": "с",
    "step_up": "Вверх",
    "step_down": "Вниз",
    "step_remove": "Удалить",
    "add_step": "Добавить шаг",
    "steps_count": "{n} из {max} шагов"
  },
  "kk": {
    "title": "Араластырғыш",
    "crumbs": "Жиілік түрлендіргіштегі шликер араластырғышы",
    "no_mixer": "Бұл микробағдарлама араластырғышсыз жиналған",
    "no_mixer_sub": "Араластырғыш қозғалтқышы тек Modbus мастер рөліне арналған микробағдарламада болады. Бұл құрылғы араластырғыш туралы хабарламайды: мұнда көрсететін де, баптайтын да ештеңе жоқ.",
    "save": "Сақтау",
    "control": "Араластырғыш",
    "control_sub": "Жалпы ажыратқыш, белсенді бағдарлама және қазір не болып жатқаны.",
    "mix_enabled": "Араластырғыш қосулы",
    "mix_prog": "Белсенді бағдарлама",
    "state": "Күйі",
    "state_stopped": "Тоқтатылған",
    "state_running": "Жұмыс істеп тұр",
    "state_error": "Қате",
    "current_step": "Ағымдағы қадам",
    "step_position": "{total} қадамның {index}-сі",
    "step_summary": "{type} · {speed} айн/мин · {duration}",
    "step_left": "Қадам соңына дейін",
    "speed": "Берілген айналым",
    "direction": "Бағыты",
    "rpm_value": "{n} айн/мин",
    "duration_min_sec": "{m} мин {s} с",
    "duration_sec": "{s} с",
    "vfd": "Жиілік түрлендіргіш",
    "vfd_sub": "Түрлендіргіш RS-485 арқылы не хабарлайды.",
    "vfd_online": "Байланыс бар",
    "vfd_offline": "Байланыс жоқ",
    "frequency": "Шығыс жиілігі",
    "current": "Қозғалтқыш тогы",
    "voltage": "Шина кернеуі",
    "temperature": "Радиатор температурасы",
    "hz": "Гц",
    "a": "А",
    "v": "В",
    "celsius": "°C",
    "hardware": "Түрлендіргішті қосу",
    "hardware_sub": "Түрлендіргіш қалай қосылған және айналым қалай қайта есептеледі.",
    "mix_rpm_50hz": "50 Гц кезіндегі білік айналымы",
    "mix_rpm_50hz_hint": "50 Гц кезінде білік минутына қанша айналым беретіні. Осы сан бойынша берілген айналым жиілікке қайта есептеледі.",
    "mix_vfd_port": "RS-485 порты",
    "mix_vfd_addr": "Түрлендіргіштің Modbus мекенжайы",
    "mix_rev_code": "Реверс команда коды",
    "mix_rev_code_hint": "Реверс үшін түрлендіргіш күтетін команда: оның ревизиясына байланысты.",
    "mix_dir_inv": "Бағытты инверттеу",
    "mix_dir_inv_hint": "Шинадағы «алға» мен «кері» орындарын ауыстырады: қозғалтқыш кері қосылған болса, бағдарламадағы қадам атаулары біліктің шын айналу бағытына сәйкес келеді.",
    "port_n": "Порт {n}",
    "editor": "Бағдарлама редакторы",
    "editor_sub": "{max} қадамға дейін: үзіліс немесе белгіленген уақыт бойы алға не кері айналу.",
    "edited_program": "Бағдарлама",
    "program_name": "Атауы",
    "program_untitled": "{n}-бағдарлама",
    "program_corrupt": "Сақталған бағдарламаны оқу мүмкін болмады, ол бос көрсетілген. Сақтау оны қайта жазады.",
    "no_steps": "Бағдарлама бос: араластырғыш ештеңе істемейді.",
    "step_type": "Қадам",
    "step_type_pause": "Үзіліс",
    "step_type_forward": "Алға",
    "step_type_reverse": "Кері",
    "step_speed": "айн/мин",
    "step_effective": "{hz} Гц · {rpm} айн/мин",
    "step_minutes": "мин",
    "step_seconds": "с",
    "step_up": "Жоғары",
    "step_down": "Төмен",
    "step_remove": "Жою",
    "add_step": "Қадам қосу",
    "steps_count": "{max} қадамның {n}-і"
  },
  "it": {
    "title": "Mescolatore",
    "crumbs": "Mescolatore per barbottina su inverter",
    "no_mixer": "Questo firmware è senza mescolatore",
    "no_mixer_sub": "Il motore del mescolatore è compilato solo nel firmware con ruolo master Modbus. Questo dispositivo non riporta alcun mescolatore: qui non c'è nulla da mostrare né da impostare.",
    "save": "Salva",
    "control": "Mescolatore",
    "control_sub": "Interruttore generale, programma attivo e cosa sta facendo in questo momento.",
    "mix_enabled": "Mescolatore attivo",
    "mix_prog": "Programma attivo",
    "state": "Stato",
    "state_stopped": "Fermo",
    "state_running": "In funzione",
    "state_error": "Errore",
    "current_step": "Passo corrente",
    "step_position": "Passo {index} di {total}",
    "step_summary": "{type} · {speed} giri/min · {duration}",
    "step_left": "Al termine del passo",
    "speed": "Velocità impostata",
    "direction": "Direzione",
    "rpm_value": "{n} giri/min",
    "duration_min_sec": "{m} min {s} s",
    "duration_sec": "{s} s",
    "vfd": "Inverter",
    "vfd_sub": "Ciò che l'inverter riporta via RS-485.",
    "vfd_online": "Collegato",
    "vfd_offline": "Non collegato",
    "frequency": "Frequenza di uscita",
    "current": "Corrente motore",
    "voltage": "Tensione del bus",
    "temperature": "Temperatura del dissipatore",
    "hz": "Hz",
    "a": "A",
    "v": "V",
    "celsius": "°C",
    "hardware": "Collegamento dell'inverter",
    "hardware_sub": "Come è collegato l'inverter e come vengono convertiti i giri.",
    "mix_rpm_50hz": "Giri dell'albero a 50 Hz",
    "mix_rpm_50hz_hint": "Giri al minuto dell'albero a 50 Hz. Con questo numero la velocità richiesta viene convertita in frequenza.",
    "mix_vfd_port": "Porta RS-485",
    "mix_vfd_addr": "Indirizzo Modbus dell'inverter",
    "mix_rev_code": "Codice comando di inversione",
    "mix_rev_code_hint": "Comando che l'inverter attende per l'inversione: dipende dalla sua revisione.",
    "mix_dir_inv": "Inverti il senso di rotazione",
    "mix_dir_inv_hint": "Scambia avanti e inversione sul bus: così i nomi dei passi del programma corrispondono al verso in cui gira davvero l'albero, se il motore è collegato al contrario.",
    "port_n": "Porta {n}",
    "editor": "Editor del programma",
    "editor_sub": "Fino a {max} passi: una pausa oppure la rotazione avanti o indietro per un tempo definito.",
    "edited_program": "Programma",
    "program_name": "Nome",
    "program_untitled": "Programma {n}",
    "program_corrupt": "Il programma salvato non è leggibile ed è mostrato vuoto. Salvando lo si sovrascrive.",
    "no_steps": "Il programma è vuoto: il mescolatore non fa nulla.",
    "step_type": "Passo",
    "step_type_pause": "Pausa",
    "step_type_forward": "Avanti",
    "step_type_reverse": "Inversione",
    "step_speed": "giri/min",
    "step_effective": "{hz} Hz · {rpm} giri/min",
    "step_minutes": "min",
    "step_seconds": "s",
    "step_up": "Su",
    "step_down": "Giù",
    "step_remove": "Rimuovi",
    "add_step": "Aggiungi passo",
    "steps_count": "{n} di {max} passi"
  },
  "de": {
    "title": "Rührwerk",
    "crumbs": "Schlicker-Rührwerk am Frequenzumrichter",
    "no_mixer": "Diese Firmware ist ohne Rührwerk gebaut",
    "no_mixer_sub": "Die Rührwerkssteuerung steckt nur in der Firmware für die Modbus-Master-Rolle. Dieses Gerät meldet kein Rührwerk: hier gibt es nichts anzuzeigen und nichts einzustellen.",
    "save": "Speichern",
    "control": "Rührwerk",
    "control_sub": "Hauptschalter, laufendes Programm und was gerade geschieht.",
    "mix_enabled": "Rührwerk eingeschaltet",
    "mix_prog": "Aktives Programm",
    "state": "Status",
    "state_stopped": "Gestoppt",
    "state_running": "Läuft",
    "state_error": "Fehler",
    "current_step": "Aktueller Schritt",
    "step_position": "Schritt {index} von {total}",
    "step_summary": "{type} · {speed} U/min · {duration}",
    "step_left": "Bis zum Schrittende",
    "speed": "Solldrehzahl",
    "direction": "Richtung",
    "rpm_value": "{n} U/min",
    "duration_min_sec": "{m} min {s} s",
    "duration_sec": "{s} s",
    "vfd": "Frequenzumrichter",
    "vfd_sub": "Was der Umrichter über RS-485 meldet.",
    "vfd_online": "Verbunden",
    "vfd_offline": "Keine Verbindung",
    "frequency": "Ausgangsfrequenz",
    "current": "Motorstrom",
    "voltage": "Zwischenkreisspannung",
    "temperature": "Kühlkörpertemperatur",
    "hz": "Hz",
    "a": "A",
    "v": "V",
    "celsius": "°C",
    "hardware": "Anschluss des Umrichters",
    "hardware_sub": "Wie der Umrichter angeschlossen ist und wie die Drehzahl umgerechnet wird.",
    "mix_rpm_50hz": "Wellendrehzahl bei 50 Hz",
    "mix_rpm_50hz_hint": "Umdrehungen pro Minute der Welle bei 50 Hz. Mit dieser Zahl wird die Solldrehzahl in eine Frequenz umgerechnet.",
    "mix_vfd_port": "RS-485-Port",
    "mix_vfd_addr": "Modbus-Adresse des Umrichters",
    "mix_rev_code": "Befehlscode für Reversierung",
    "mix_rev_code_hint": "Befehl, den der Umrichter für die Reversierung erwartet: abhängig von seiner Revision.",
    "mix_dir_inv": "Drehrichtung umkehren",
    "mix_dir_inv_hint": "Vertauscht vorwärts und rückwärts auf dem Bus: so passen die Schrittnamen im Programm zu der Richtung, in die sich die Welle wirklich dreht, wenn der Motor andersherum angeschlossen ist.",
    "port_n": "Port {n}",
    "editor": "Programmeditor",
    "editor_sub": "Bis zu {max} Schritte: eine Pause oder Drehen vorwärts bzw. rückwärts für eine feste Zeit.",
    "edited_program": "Programm",
    "program_name": "Name",
    "program_untitled": "Programm {n}",
    "program_corrupt": "Das gespeicherte Programm ist nicht lesbar und wird leer angezeigt. Speichern überschreibt es.",
    "no_steps": "Das Programm ist leer: das Rührwerk tut nichts.",
    "step_type": "Schritt",
    "step_type_pause": "Pause",
    "step_type_forward": "Vorwärts",
    "step_type_reverse": "Rückwärts",
    "step_speed": "U/min",
    "step_effective": "{hz} Hz · {rpm} U/min",
    "step_minutes": "min",
    "step_seconds": "s",
    "step_up": "Hoch",
    "step_down": "Runter",
    "step_remove": "Entfernen",
    "add_step": "Schritt hinzufügen",
    "steps_count": "{n} von {max} Schritten"
  }
}
</i18n>
