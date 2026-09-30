<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, shallowRef, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useAlerts } from '@/common/alert';
import { useConfirm } from '@/common/confirm';
import {
  alertFpFailure,
  cancelFpEnroll,
  deleteFpFinger,
  deleteFpUser,
  fetchFpImage,
  fetchFpUsers,
  fpErrorMessage,
  saveFpUser,
  startFpEnroll,
  useFpStatus,
} from '@/common/fpAcs';
import type { FpEnrollState, FpEventResult, FpEventSource, FpUser, FpUserRequest, Uptime } from '@/common/types';
import { useUptime } from '@/common/uptime';
import Button from '@/components/Button.vue';
import FingerprintImage from '@/components/FingerprintImage.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InfoRow from '@/components/InfoRow.vue';
import Layout from '@/components/Layout.vue';
import Modal from '@/components/Modal.vue';
import Switch from '@/components/Switch.vue';
import { api } from '@/utils/api';
import { onCustomValidation } from '@/utils/validation';

type UserFlag = 'enabled' | 'allow_fp' | 'allow_tg';

interface UserDraft {
  id?: number;
  name: string;
  tg_username: string;
  enabled: boolean;
  allow_fp: boolean;
  allow_tg: boolean;
}

interface GrayImage {
  pixels: Uint8Array;
  width: number;
  height: number;
}

const DASH = '—';
const MS_PER_S = 1000;
const S_PER_MIN = 60;
const S_PER_H = 3600;
const S_PER_D = 86400;
const DURATION_UNITS_SHOWN = 2;

const USER_FLAGS: UserFlag[] = ['enabled', 'allow_fp', 'allow_tg'];
const USER_FLAG_KEYS: Record<UserFlag, string> = {
  enabled: 'user_enabled',
  allow_fp: 'user_allow_fp',
  allow_tg: 'user_allow_tg',
};
const ENROLL_ACTIVE_STATES: FpEnrollState[] = ['place', 'lift', 'processing'];
const ENROLL_STATE_KEYS: Record<string, string> = {
  idle: 'enroll_idle',
  place: 'enroll_place',
  lift: 'enroll_lift',
  processing: 'enroll_processing',
  cancelled: 'enroll_cancelled',
};
const RESULT_KEYS: Record<FpEventResult, string> = {
  granted: 'result_granted',
  denied: 'result_denied',
  unknown: 'result_unknown',
};
const RESULT_PILL_CLASSES: Record<FpEventResult, string> = {
  granted: 'ok',
  denied: 'danger',
  unknown: 'muted',
};
const SOURCE_KEYS: Record<FpEventSource, string> = {
  fingerprint: 'source_fingerprint',
  telegram: 'source_telegram',
};

const { t } = useI18n();
const { showAlert } = useAlerts();
const { confirmDialog } = useConfirm();
const { uptime } = useUptime();

// Opened by "Add finger" or by an enrollment started elsewhere (MQTT, another browser); it stays
// open on the outcome until closed, and while it is open the status is polled at the enrollment rate.
const enrollOpen = ref(false);
const { status, failed, startPolling, stopPolling } = useFpStatus(enrollOpen);

onMounted(startPolling);
onBeforeUnmount(stopPolling);

// Firmware enums through a key table: a value this page does not know yet is shown as sent.
const enumLabel = (keys: Record<string, string>, value: string) => {
  const key = keys[value];
  return key ? t(key) : value;
};

// The two most significant units: "2 d 3 h", "4 min 12 s", "7 s".
const formatDuration = (totalSeconds: number) => {
  const seconds = Math.floor(totalSeconds);
  const parts = [
    { key: 'dur_d', n: Math.floor(seconds / S_PER_D) },
    { key: 'dur_h', n: Math.floor((seconds % S_PER_D) / S_PER_H) },
    { key: 'dur_min', n: Math.floor((seconds % S_PER_H) / S_PER_MIN) },
    { key: 'dur_s', n: seconds % S_PER_MIN },
  ];
  const first = parts.findIndex((part) => part.n > 0);
  const start = first < 0 ? parts.length - 1 : first;
  return parts
    .slice(start, start + DURATION_UNITS_SHOWN)
    .map((part) => t(part.key, { n: part.n }))
    .join(' ');
};

// The event carries the device uptime it happened at, so its age is the device uptime now minus
// that. "Now" is extrapolated from the last /uptime answer: the global poll refreshes it every
// 10 s, and one request of our own on mount keeps the first reading from being that stale.
const uptimeAnchor = shallowRef<{ seconds: number; atMs: number } | null>(null);
const anchorUptime = (value: Uptime | null) => {
  if (value) {
    const seconds = (value.days * S_PER_D) + (value.hours * S_PER_H) + (value.minutes * S_PER_MIN) + value.seconds;
    uptimeAnchor.value = { seconds, atMs: performance.now() };
  }
};
watch(uptime, anchorUptime);
onMounted(async () => {
  try {
    anchorUptime(await api<Uptime>('uptime'));
  } catch (err) {
    console.error('GET /uptime failed', err);
  }
});

// Advanced by every status answer, which is what re-renders the age once a second.
const nowMs = ref(performance.now());
watch(status, () => {
  nowMs.value = performance.now();
});

const lastEvent = computed(() => status.value?.last_event ?? null);

const lastEventAge = computed(() => {
  const anchor = uptimeAnchor.value;
  if (!lastEvent.value || !anchor) {
    return DASH;
  }
  const deviceNowS = anchor.seconds + ((nowMs.value - anchor.atMs) / MS_PER_S);
  return t('ago', { v: formatDuration(Math.max(0, deviceNowS - lastEvent.value.uptime_s)) });
});

// The image is re-read only when its seq changes. The seq is remembered only once the read
// succeeded, so a failed read is retried on the next status answer.
const image = shallowRef<GrayImage | null>(null);
let imageSeq: number | null = null;
let imageLoading = false;
watch(() => status.value?.image, async (info) => {
  if (info === undefined) {
    return;
  }
  if (info === null) {
    image.value = null;
    imageSeq = null;
    return;
  }
  if ((info.seq === imageSeq) || imageLoading) {
    return;
  }
  imageLoading = true;
  try {
    const pixels = await fetchFpImage();
    imageSeq = info.seq;
    image.value = pixels ? { pixels, width: info.width, height: info.height } : null;
  } catch (err) {
    console.error('GET /fp/image failed', err);
  } finally {
    imageLoading = false;
  }
});

const users = ref<FpUser[]>([]);
const maxUsers = ref(0);
const maxFingers = ref(0);
const busy = ref(false);

const loadUsers = async () => {
  try {
    const response = await fetchFpUsers();
    users.value = response.users;
    maxUsers.value = response.max_users;
    maxFingers.value = response.max_fingers;
  } catch (err) {
    await alertFpFailure(err);
  }
};

// users_rev moves whenever the list changes, from this page or from anywhere else (MQTT).
watch(() => status.value?.users_rev, (rev) => {
  if (rev !== undefined) {
    loadUsers();
  }
});

// Every change is followed by a re-read of the list: it shows the device's own view of the result,
// and it puts back a switch the device refused to flip.
const mutate = async (request: () => Promise<unknown>): Promise<boolean> => {
  busy.value = true;
  try {
    await request();
    return true;
  } catch (err) {
    await alertFpFailure(err);
    return false;
  } finally {
    busy.value = false;
    await loadUsers();
  }
};

const setUserFlag = (user: FpUser, flag: UserFlag, value: boolean) => {
  // Flipped locally first: the switch must not jump back while the request is on its way.
  user[flag] = value;
  const request: FpUserRequest = { id: user.id, name: user.name };
  request[flag] = value;
  mutate(() => saveFpUser(request));
};

const draft = ref<UserDraft | null>(null);

const toDraft = (user: FpUser): UserDraft => ({
  id: user.id,
  name: user.name,
  tg_username: user.tg_username,
  enabled: user.enabled,
  allow_fp: user.allow_fp,
  allow_tg: user.allow_tg,
});

// The live record of the user being edited: its fingers change under enrollment and deletion.
const draftUser = computed(() => users.value.find((user) => user.id === draft.value?.id));

const newUser = () => {
  draft.value = { name: '', tg_username: '', enabled: true, allow_fp: true, allow_tg: false };
};

const editUser = (user: FpUser) => {
  draft.value = toDraft(user);
};

const saveDraft = () => mutate(async () => {
  const { user } = await saveFpUser({ ...draft.value! });
  // A created user stays open in the form, so fingers can be added to it right away.
  draft.value = toDraft(user);
  showAlert('data_updated', { type: 'success' });
});

const removeUser = async (user: FpUser) => {
  if (!(await confirmDialog(t('delete_user_confirm', { name: user.name }), t('delete')))) {
    return;
  }
  await mutate(() => deleteFpUser(user.id));
};

const removeFinger = async (slot: number) => {
  if (!(await confirmDialog(t('delete_finger_confirm', { slot }), t('delete')))) {
    return;
  }
  mutate(() => deleteFpFinger(slot));
};

const enroll = computed(() => status.value?.enroll);
const enrollActive = computed(() => !!enroll.value && ENROLL_ACTIVE_STATES.includes(enroll.value.state));
const enrollUser = computed(() => users.value.find((user) => user.id === enroll.value?.user_id));

// An enrollment started elsewhere opens the editor on its user. The record is watched too: on page
// load the status reporting the enrollment comes before the users list. A form already open is left
// as it is, so nothing typed into it is lost.
watch([enrollActive, enrollUser], ([active, user]) => {
  if (!active) {
    return;
  }
  enrollOpen.value = true;
  if (user && !draft.value) {
    draft.value = toDraft(user);
  }
});

const enrollUserName = computed(() => enrollUser.value?.name ?? DASH);

const editorOpen = computed(() => !!draft.value || enrollOpen.value);

const editorTitle = computed(() => {
  if (!draft.value) {
    return t('enroll_title');
  }
  return (draft.value.id === undefined) ? t('new_user') : t('edit_user');
});

// A running enrollment is followed and cancelled in the editor, so it stays open until the
// enrollment is cancelled or finishes.
const closeEditor = () => {
  if (enrollActive.value) {
    return;
  }
  draft.value = null;
  enrollOpen.value = false;
};

const enrollMessage = computed(() => {
  const value = enroll.value;
  if (!value) {
    return '';
  }
  if (value.state === 'done') {
    return t('enroll_done', { slot: value.slot ?? DASH });
  }
  if (value.state === 'error') {
    return t('enroll_error', { error: value.error ? t(fpErrorMessage(value.error)) : DASH });
  }
  return enumLabel(ENROLL_STATE_KEYS, value.state);
});

const addFinger = async (user: FpUser) => {
  if (await mutate(() => startFpEnroll(user.id))) {
    enrollOpen.value = true;
    // Poll at once at the enrollment rate; a status answer read before the start is dropped.
    startPolling();
  }
};

const cancelEnroll = () => mutate(cancelFpEnroll);
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <Info v-if="failed" severity="error" :text="status ? t('request_failed') : t('request_failed_no_data')" />

      <template v-if="status">
        <div class="grid-2">
          <section class="card">
            <div class="card-header">
              <div class="title">{{ t('sensor') }}</div>
              <span v-if="status.sensor.online" class="pill ok"><span class="dot" />{{ t('sensor_online') }}</span>
              <span v-else class="pill danger"><span class="dot" />{{ t('sensor_offline') }}</span>
            </div>
            <div class="card-body">
              <InfoRow :label="t('templates')">
                <span class="mono">{{ status.sensor.used }} / {{ status.sensor.capacity }}</span>
              </InfoRow>
            </div>
          </section>

          <section class="card">
            <div class="card-header">
              <div class="title">{{ t('last_event') }}</div>
              <span v-if="lastEvent" class="pill" :class="RESULT_PILL_CLASSES[lastEvent.result]">
                {{ enumLabel(RESULT_KEYS, lastEvent.result) }}
              </span>
            </div>
            <div class="card-body access-event">
              <div class="access-eventInfo">
                <template v-if="lastEvent">
                  <InfoRow :label="t('event_source')">{{ enumLabel(SOURCE_KEYS, lastEvent.source) }}</InfoRow>
                  <InfoRow :label="t('event_user')">{{ lastEvent.user_name ?? DASH }}</InfoRow>
                  <InfoRow :label="t('event_time')">{{ lastEventAge }}</InfoRow>
                </template>
                <p v-else class="access-note">{{ t('no_events') }}</p>
              </div>
              <FingerprintImage v-if="image" class="access-image" v-bind="image" :label="t('image_label')" />
            </div>
          </section>
        </div>

        <section class="card">
          <div class="card-header">
            <div class="card-title-wrap">
              <div class="title">{{ t('users') }}</div>
              <div class="sub">{{ t('users_sub', { n: users.length, max: maxUsers }) }}</div>
            </div>
            <Button type="button" :disabled="busy || (users.length >= maxUsers)" @click="newUser">
              {{ t('add_user') }}
            </Button>
          </div>
          <div class="card-body">
            <p v-if="!users.length" class="access-note">{{ t('no_users') }}</p>
            <div v-else class="access-tableWrap">
              <table class="access-table">
                <thead>
                  <tr>
                    <th>{{ t('user_name') }}</th>
                    <th>{{ t('col_enabled') }}</th>
                    <th>{{ t('col_fp') }}</th>
                    <th>{{ t('col_tg') }}</th>
                    <th>{{ t('user_tg_username') }}</th>
                    <th>{{ t('user_fingers') }}</th>
                    <th />
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="user in users" :key="user.id" class="access-row">
                    <td>{{ user.name }}</td>
                    <td v-for="flag in USER_FLAGS" :key="flag" class="access-cellCenter">
                      <Switch
                        :id="`user_${user.id}_${flag}`"
                        :model-value="user[flag]"
                        :aria-label="t(USER_FLAG_KEYS[flag])"
                        :disabled="busy"
                        @update:model-value="(value) => setUserFlag(user, flag, !!value)"
                      />
                    </td>
                    <td class="mono">{{ user.tg_username ? `@${user.tg_username}` : DASH }}</td>
                    <td class="mono access-cellCenter">{{ user.fingers.length }}</td>
                    <td>
                      <div class="access-actions">
                        <Button type="button" variant="outline" @click="editUser(user)">{{ t('edit') }}</Button>
                        <Button type="button" variant="danger" :disabled="busy" @click="removeUser(user)">
                          {{ t('delete') }}
                        </Button>
                      </div>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>
        </section>

        <Modal :open="editorOpen" @close="closeEditor">
          <template #title>{{ editorTitle }}</template>
          <template #actions>
            <Button type="button" variant="outline" :disabled="enrollActive" @click="closeEditor">
              {{ t('close') }}
            </Button>
            <Button v-if="draft" type="submit" form="user_form" :is-loading="busy" :disabled="busy">
              {{ t('save') }}
            </Button>
          </template>

          <form v-if="draft" id="user_form" @submit.prevent="saveDraft">
            <div class="field">
              <label for="user_name">{{ t('user_name') }}</label>
              <input id="user_name" v-model="draft.name" type="text" name="user_name" required autofocus />
            </div>
            <div class="field">
              <label for="user_tg_username">{{ t('user_tg_username') }}</label>
              <input
                id="user_tg_username"
                v-model="draft.tg_username"
                type="text"
                name="user_tg_username"
                class="mono"
                pattern="^[A-Za-z0-9_]{0,32}$"
                maxlength="32"
                :placeholder="t('tg_username_placeholder')"
                @input="(ev) => onCustomValidation(ev, t('wrong_tg_username_pattern'))"
              />
            </div>
            <div v-for="flag in USER_FLAGS" :key="flag" class="field">
              <label :for="`user_${flag}`">{{ t(USER_FLAG_KEYS[flag]) }}</label>
              <div class="access-switchEnd"><Switch :id="`user_${flag}`" v-model="draft[flag]" /></div>
            </div>
          </form>

          <div v-if="draftUser" class="sub-section">
            <div class="access-sectionHeader">
              <div class="sub-section-label">
                {{ t('fingers', { n: draftUser.fingers.length, max: maxFingers }) }}
              </div>
              <Button
                type="button"
                variant="outline"
                :disabled="busy || enrollActive || (draftUser.fingers.length >= maxFingers)"
                @click="addFinger(draftUser)"
              >
                {{ t('add_finger') }}
              </Button>
            </div>
            <p v-if="!draftUser.fingers.length" class="access-note">{{ t('no_fingers') }}</p>
            <ul v-else class="access-fingers">
              <li v-for="slot in draftUser.fingers" :key="slot" class="access-finger">
                <span class="mono">{{ t('finger_slot', { slot }) }}</span>
                <Button type="button" variant="danger" :disabled="busy" @click="removeFinger(slot)">
                  {{ t('delete') }}
                </Button>
              </li>
            </ul>
          </div>

          <div v-if="enrollOpen && enroll" class="sub-section">
            <div class="access-sectionHeader">
              <!-- Without the form the modal title already says this, and its Close does the same. -->
              <div v-if="draft" class="sub-section-label">{{ t('enroll_title') }}</div>
              <Button v-if="enrollActive" type="button" variant="danger" :disabled="busy" @click="cancelEnroll">
                {{ t('cancel') }}
              </Button>
              <Button v-else-if="draft" type="button" variant="outline" @click="enrollOpen = false">
                {{ t('close') }}
              </Button>
            </div>
            <div class="access-event">
              <div class="access-eventInfo">
                <InfoRow :label="t('event_user')">{{ enrollUserName }}</InfoRow>
                <InfoRow v-if="enrollActive" :label="t('enroll_progress')">
                  <span class="mono">{{ t('enroll_step', { n: enroll.step, m: enroll.steps }) }}</span>
                </InfoRow>
                <p class="access-enrollMessage" :class="{ 'access-enrollMessageError': enroll.state === 'error' }">
                  {{ enrollMessage }}
                </p>
              </div>
              <FingerprintImage v-if="image" class="access-image" v-bind="image" :label="t('image_label')" />
            </div>
          </div>
        </Modal>
      </template>
    </div>
  </Layout>
</template>

<style scoped>
.access-event {
  display: flex;
  flex-wrap: wrap;
  align-items: flex-start;
  gap: 16px;
}

.access-eventInfo {
  flex: 1 1 220px;
  min-width: 0;
}

.access-image {
  flex: 0 0 auto;
  width: 128px;
  margin-top: 8px;
}

.access-note {
  margin: 12px 0 4px;
  color: var(--text-muted);
}

.access-tableWrap {
  margin-top: 8px;
  overflow-x: auto;
}

.access-table {
  width: 100%;
}

.access-row td {
  vertical-align: middle;
}

.access-cellCenter {
  text-align: center;
}

.access-actions {
  display: flex;
  justify-content: flex-end;
  gap: 6px;
}

.access-switchEnd {
  justify-self: end;
}

.access-sectionHeader {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.access-fingers {
  list-style: none;
  margin: 8px 0 0;
  padding: 0;
}

.access-finger {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 6px 0;
  border-bottom: 1px dashed var(--border-color);
}

.access-finger:last-child {
  border-bottom: 0;
}

.access-enrollMessage {
  margin: 12px 0 4px;
  font-size: 14px;
  font-weight: 500;
}

.access-enrollMessageError {
  color: var(--danger-color);
}
</style>

<i18n>
{
  "en": {
    "title": "Access",
    "crumbs": "Fingerprint sensor, users and access events",
    "request_failed": "The device does not answer: the values below may be out of date",
    "request_failed_no_data": "Could not get the access control state from the device",

    "sensor": "Fingerprint sensor",
    "sensor_online": "Online",
    "sensor_offline": "Offline",
    "templates": "Fingerprints stored",

    "last_event": "Last event",
    "no_events": "No access events since the device started",
    "event_source": "Source",
    "event_user": "User",
    "event_time": "When",
    "ago": "{v} ago",
    "source_fingerprint": "Fingerprint",
    "source_telegram": "Telegram",
    "result_granted": "Granted",
    "result_denied": "Denied",
    "result_unknown": "Not recognized",
    "image_label": "Last fingerprint image",

    "users": "Users",
    "users_sub": "{n} of {max}",
    "add_user": "Add user",
    "no_users": "No users yet",
    "user_name": "Name",
    "col_enabled": "Enabled",
    "col_fp": "Fingerprint",
    "col_tg": "Telegram",
    "user_enabled": "Enabled",
    "user_allow_fp": "Access by fingerprint",
    "user_allow_tg": "Access via Telegram",
    "user_tg_username": "Telegram username",
    "user_fingers": "Fingers",
    "edit": "Edit",
    "delete": "Delete",
    "delete_user_confirm": "Delete user {name} with all their fingerprints?",
    "new_user": "New user",
    "edit_user": "Edit user",
    "close": "Close",
    "tg_username_placeholder": "without {'@'}",
    "wrong_tg_username_pattern": "Enter up to 32 characters: Latin letters, numbers and underscore, without {'@'}",

    "fingers": "Fingers: {n} of {max}",
    "add_finger": "Add finger",
    "no_fingers": "No fingers enrolled",
    "finger_slot": "Slot {slot}",
    "delete_finger_confirm": "Delete the fingerprint in slot {slot}?",

    "enroll_title": "Adding a finger",
    "enroll_progress": "Step",
    "enroll_step": "{n} of {m}",
    "enroll_idle": "Waiting for the sensor…",
    "enroll_place": "Place the finger on the sensor",
    "enroll_lift": "Lift the finger",
    "enroll_processing": "Processing…",
    "enroll_done": "Finger added to slot {slot}",
    "enroll_error": "Enrollment failed: {error}",
    "enroll_cancelled": "Enrollment cancelled",
    "cancel": "Cancel",

    "dur_d": "{n} d",
    "dur_h": "{n} h",
    "dur_min": "{n} min",
    "dur_s": "{n} s"
  },
  "ru": {
    "title": "Доступ",
    "crumbs": "Датчик отпечатков, пользователи и события доступа",
    "request_failed": "Устройство не отвечает: значения ниже могут быть устаревшими",
    "request_failed_no_data": "Не удалось получить от устройства состояние контроля доступа",

    "sensor": "Датчик отпечатков",
    "sensor_online": "На связи",
    "sensor_offline": "Нет связи",
    "templates": "Сохранено отпечатков",

    "last_event": "Последнее событие",
    "no_events": "С запуска устройства событий доступа не было",
    "event_source": "Источник",
    "event_user": "Пользователь",
    "event_time": "Когда",
    "ago": "{v} назад",
    "source_fingerprint": "Отпечаток",
    "source_telegram": "Telegram",
    "result_granted": "Разрешён",
    "result_denied": "Запрещён",
    "result_unknown": "Не распознан",
    "image_label": "Последний снимок отпечатка",

    "users": "Пользователи",
    "users_sub": "{n} из {max}",
    "add_user": "Добавить пользователя",
    "no_users": "Пользователей пока нет",
    "user_name": "Имя",
    "col_enabled": "Включён",
    "col_fp": "Отпечаток",
    "col_tg": "Telegram",
    "user_enabled": "Включён",
    "user_allow_fp": "Доступ по отпечатку",
    "user_allow_tg": "Доступ через Telegram",
    "user_tg_username": "Имя в Telegram",
    "user_fingers": "Пальцы",
    "edit": "Изменить",
    "delete": "Удалить",
    "delete_user_confirm": "Удалить пользователя {name} вместе со всеми его отпечатками?",
    "new_user": "Новый пользователь",
    "edit_user": "Редактирование пользователя",
    "close": "Закрыть",
    "tg_username_placeholder": "без {'@'}",
    "wrong_tg_username_pattern": "Введите до 32 символов: латиница, цифры и подчёркивание, без {'@'}",

    "fingers": "Пальцы: {n} из {max}",
    "add_finger": "Добавить палец",
    "no_fingers": "Нет записанных пальцев",
    "finger_slot": "Ячейка {slot}",
    "delete_finger_confirm": "Удалить отпечаток из ячейки {slot}?",

    "enroll_title": "Запись пальца",
    "enroll_progress": "Шаг",
    "enroll_step": "{n} из {m}",
    "enroll_idle": "Ожидание датчика…",
    "enroll_place": "Приложите палец к датчику",
    "enroll_lift": "Уберите палец",
    "enroll_processing": "Обработка…",
    "enroll_done": "Палец записан в ячейку {slot}",
    "enroll_error": "Не удалось записать палец: {error}",
    "enroll_cancelled": "Запись отменена",
    "cancel": "Отменить",

    "dur_d": "{n} д",
    "dur_h": "{n} ч",
    "dur_min": "{n} мин",
    "dur_s": "{n} с"
  },
  "kk": {
    "title": "Қолжетімділік",
    "crumbs": "Саусақ ізі датчигі, пайдаланушылар және қолжетімділік оқиғалары",
    "request_failed": "Құрылғы жауап бермейді: төмендегі мәндер ескірген болуы мүмкін",
    "request_failed_no_data": "Құрылғыдан қолжетімділікті бақылау күйін алу мүмкін болмады",

    "sensor": "Саусақ ізі датчигі",
    "sensor_online": "Байланыста",
    "sensor_offline": "Байланыс жоқ",
    "templates": "Сақталған саусақ іздері",

    "last_event": "Соңғы оқиға",
    "no_events": "Құрылғы іске қосылғаннан бері қолжетімділік оқиғалары болған жоқ",
    "event_source": "Көзі",
    "event_user": "Пайдаланушы",
    "event_time": "Қашан",
    "ago": "{v} бұрын",
    "source_fingerprint": "Саусақ ізі",
    "source_telegram": "Telegram",
    "result_granted": "Рұқсат етілді",
    "result_denied": "Тыйым салынды",
    "result_unknown": "Танылмады",
    "image_label": "Саусақ ізінің соңғы суреті",

    "users": "Пайдаланушылар",
    "users_sub": "{n} / {max}",
    "add_user": "Пайдаланушы қосу",
    "no_users": "Әзірге пайдаланушылар жоқ",
    "user_name": "Аты",
    "col_enabled": "Қосулы",
    "col_fp": "Саусақ ізі",
    "col_tg": "Telegram",
    "user_enabled": "Қосулы",
    "user_allow_fp": "Саусақ ізі арқылы кіру",
    "user_allow_tg": "Telegram арқылы кіру",
    "user_tg_username": "Telegram-дағы аты",
    "user_fingers": "Саусақтар",
    "edit": "Өзгерту",
    "delete": "Жою",
    "delete_user_confirm": "{name} пайдаланушысын барлық саусақ іздерімен бірге жою керек пе?",
    "new_user": "Жаңа пайдаланушы",
    "edit_user": "Пайдаланушыны өңдеу",
    "close": "Жабу",
    "tg_username_placeholder": "{'@'} белгісіз",
    "wrong_tg_username_pattern": "32 таңбаға дейін енгізіңіз: латын әріптері, сандар және астыңғы сызық, {'@'} белгісіз",

    "fingers": "Саусақтар: {n} / {max}",
    "add_finger": "Саусақ қосу",
    "no_fingers": "Жазылған саусақтар жоқ",
    "finger_slot": "Ұяшық {slot}",
    "delete_finger_confirm": "{slot} ұяшығындағы саусақ ізін жою керек пе?",

    "enroll_title": "Саусақты жазу",
    "enroll_progress": "Қадам",
    "enroll_step": "{n} / {m}",
    "enroll_idle": "Датчикті күту…",
    "enroll_place": "Саусақты датчикке қойыңыз",
    "enroll_lift": "Саусақты алыңыз",
    "enroll_processing": "Өңделуде…",
    "enroll_done": "Саусақ {slot} ұяшығына жазылды",
    "enroll_error": "Саусақты жазу мүмкін болмады: {error}",
    "enroll_cancelled": "Жазу тоқтатылды",
    "cancel": "Болдырмау",

    "dur_d": "{n} күн",
    "dur_h": "{n} сағ",
    "dur_min": "{n} мин",
    "dur_s": "{n} с"
  },
  "it": {
    "title": "Accesso",
    "crumbs": "Sensore di impronte, utenti ed eventi di accesso",
    "request_failed": "Il dispositivo non risponde: i valori qui sotto potrebbero non essere aggiornati",
    "request_failed_no_data": "Impossibile ottenere dal dispositivo lo stato del controllo accessi",

    "sensor": "Sensore di impronte",
    "sensor_online": "Online",
    "sensor_offline": "Offline",
    "templates": "Impronte memorizzate",

    "last_event": "Ultimo evento",
    "no_events": "Nessun evento di accesso dall'avvio del dispositivo",
    "event_source": "Origine",
    "event_user": "Utente",
    "event_time": "Quando",
    "ago": "{v} fa",
    "source_fingerprint": "Impronta",
    "source_telegram": "Telegram",
    "result_granted": "Consentito",
    "result_denied": "Negato",
    "result_unknown": "Non riconosciuto",
    "image_label": "Ultima immagine dell'impronta",

    "users": "Utenti",
    "users_sub": "{n} di {max}",
    "add_user": "Aggiungi utente",
    "no_users": "Ancora nessun utente",
    "user_name": "Nome",
    "col_enabled": "Abilitato",
    "col_fp": "Impronta",
    "col_tg": "Telegram",
    "user_enabled": "Abilitato",
    "user_allow_fp": "Accesso con impronta",
    "user_allow_tg": "Accesso tramite Telegram",
    "user_tg_username": "Username Telegram",
    "user_fingers": "Dita",
    "edit": "Modifica",
    "delete": "Elimina",
    "delete_user_confirm": "Eliminare l'utente {name} con tutte le sue impronte?",
    "new_user": "Nuovo utente",
    "edit_user": "Modifica utente",
    "close": "Chiudi",
    "tg_username_placeholder": "senza {'@'}",
    "wrong_tg_username_pattern": "Inserisci fino a 32 caratteri: lettere latine, numeri e trattino basso, senza {'@'}",

    "fingers": "Dita: {n} di {max}",
    "add_finger": "Aggiungi dito",
    "no_fingers": "Nessun dito registrato",
    "finger_slot": "Slot {slot}",
    "delete_finger_confirm": "Eliminare l'impronta nello slot {slot}?",

    "enroll_title": "Registrazione del dito",
    "enroll_progress": "Passo",
    "enroll_step": "{n} di {m}",
    "enroll_idle": "In attesa del sensore…",
    "enroll_place": "Appoggia il dito sul sensore",
    "enroll_lift": "Solleva il dito",
    "enroll_processing": "Elaborazione…",
    "enroll_done": "Dito registrato nello slot {slot}",
    "enroll_error": "Registrazione non riuscita: {error}",
    "enroll_cancelled": "Registrazione annullata",
    "cancel": "Annulla",

    "dur_d": "{n} g",
    "dur_h": "{n} h",
    "dur_min": "{n} min",
    "dur_s": "{n} s"
  },
  "de": {
    "title": "Zugang",
    "crumbs": "Fingerabdrucksensor, Benutzer und Zugangsereignisse",
    "request_failed": "Das Gerät antwortet nicht: Die Werte unten sind möglicherweise veraltet",
    "request_failed_no_data": "Der Zustand der Zugangskontrolle konnte nicht vom Gerät abgerufen werden",

    "sensor": "Fingerabdrucksensor",
    "sensor_online": "Online",
    "sensor_offline": "Offline",
    "templates": "Gespeicherte Fingerabdrücke",

    "last_event": "Letztes Ereignis",
    "no_events": "Seit dem Start des Geräts keine Zugangsereignisse",
    "event_source": "Quelle",
    "event_user": "Benutzer",
    "event_time": "Wann",
    "ago": "vor {v}",
    "source_fingerprint": "Fingerabdruck",
    "source_telegram": "Telegram",
    "result_granted": "Gewährt",
    "result_denied": "Verweigert",
    "result_unknown": "Nicht erkannt",
    "image_label": "Letztes Fingerabdruckbild",

    "users": "Benutzer",
    "users_sub": "{n} von {max}",
    "add_user": "Benutzer hinzufügen",
    "no_users": "Noch keine Benutzer",
    "user_name": "Name",
    "col_enabled": "Aktiviert",
    "col_fp": "Fingerabdruck",
    "col_tg": "Telegram",
    "user_enabled": "Aktiviert",
    "user_allow_fp": "Zugang per Fingerabdruck",
    "user_allow_tg": "Zugang über Telegram",
    "user_tg_username": "Telegram-Benutzername",
    "user_fingers": "Finger",
    "edit": "Bearbeiten",
    "delete": "Löschen",
    "delete_user_confirm": "Benutzer {name} mit allen Fingerabdrücken löschen?",
    "new_user": "Neuer Benutzer",
    "edit_user": "Benutzer bearbeiten",
    "close": "Schließen",
    "tg_username_placeholder": "ohne {'@'}",
    "wrong_tg_username_pattern": "Geben Sie bis zu 32 Zeichen ein: lateinische Buchstaben, Zahlen und Unterstrich, ohne {'@'}",

    "fingers": "Finger: {n} von {max}",
    "add_finger": "Finger hinzufügen",
    "no_fingers": "Keine Finger registriert",
    "finger_slot": "Platz {slot}",
    "delete_finger_confirm": "Fingerabdruck auf Platz {slot} löschen?",

    "enroll_title": "Finger registrieren",
    "enroll_progress": "Schritt",
    "enroll_step": "{n} von {m}",
    "enroll_idle": "Warten auf den Sensor…",
    "enroll_place": "Legen Sie den Finger auf den Sensor",
    "enroll_lift": "Heben Sie den Finger ab",
    "enroll_processing": "Verarbeitung…",
    "enroll_done": "Finger auf Platz {slot} registriert",
    "enroll_error": "Registrierung fehlgeschlagen: {error}",
    "enroll_cancelled": "Registrierung abgebrochen",
    "cancel": "Abbrechen",

    "dur_d": "{n} T",
    "dur_h": "{n} h",
    "dur_min": "{n} min",
    "dur_s": "{n} s"
  }
}
</i18n>
