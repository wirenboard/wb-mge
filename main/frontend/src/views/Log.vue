<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { alertFpFailure, fetchFpLog, useFpStatus } from '@/common/fpAcs';
import type { FpEventResult, FpEventSource, FpLogEntry } from '@/common/types';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import Layout from '@/components/Layout.vue';

const DASH = '—';
const MS_PER_S = 1000;

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
  mqtt: 'source_mqtt',
};

const { t, locale } = useI18n();

const { status, failed, startPolling, stopPolling } = useFpStatus(ref(false));

onMounted(startPolling);
onBeforeUnmount(stopPolling);

// Firmware enums through a key table: a value this page does not know yet is shown as sent.
const enumLabel = (keys: Record<string, string>, value: string) => {
  const key = keys[value];
  return key ? t(key) : value;
};

const sourceLabel = (source: FpEventSource, chat: string | null) => {
  const label = enumLabel(SOURCE_KEYS, source);
  return chat ? t('source_chat', { source: label, chat }) : label;
};

const dateFormat = computed(() => new Intl.DateTimeFormat(locale.value, { dateStyle: 'short', timeStyle: 'medium' }));

const formatTime = (time: number | null) => (time === null ? DASH : dateFormat.value.format(time * MS_PER_S));

const entries = ref<FpLogEntry[] | null>(null);
// Only the answer to the latest request is shown: an earlier one that lands after it is older.
let loadRun = 0;

const loadLog = async () => {
  const run = ++loadRun;
  try {
    const response = await fetchFpLog();
    if (run === loadRun) {
      entries.value = response.entries;
    }
  } catch (err) {
    await alertFpFailure(err);
  }
};

// Every attempt moves last_event.seq, so the log is read on the first status answer and again on
// each change. 0 stands for "no event since boot", undefined for "no status yet".
const lastEventSeq = computed(() => (status.value ? (status.value.last_event?.seq ?? 0) : undefined));
watch(lastEventSeq, (seq) => {
  if (seq !== undefined) {
    loadLog();
  }
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <Info v-if="failed" severity="error" :text="status ? t('request_failed') : t('request_failed_no_data')" />

      <section v-if="entries" class="card">
        <div class="card-header">
          <div class="title">{{ t('attempts') }}</div>
        </div>
        <div class="card-body">
          <p v-if="!entries.length" class="log-note">{{ t('no_entries') }}</p>
          <div v-else class="log-tableWrap">
            <table class="log-table">
              <thead>
                <tr>
                  <th>{{ t('col_time') }}</th>
                  <th>{{ t('col_user') }}</th>
                  <th>{{ t('col_source') }}</th>
                  <th>{{ t('col_result') }}</th>
                </tr>
              </thead>
              <tbody>
                <tr v-for="entry in entries" :key="entry.seq" class="log-row">
                  <td class="mono">{{ formatTime(entry.time) }}</td>
                  <td>{{ entry.user_name ?? DASH }}</td>
                  <td>{{ sourceLabel(entry.source, entry.chat) }}</td>
                  <td>
                    <span class="pill" :class="RESULT_PILL_CLASSES[entry.result]">
                      {{ enumLabel(RESULT_KEYS, entry.result) }}
                    </span>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </section>
    </div>
  </Layout>
</template>

<style scoped>
.log-note {
  margin: 12px 0 4px;
  color: var(--text-muted);
}

.log-tableWrap {
  margin-top: 8px;
  overflow-x: auto;
}

.log-table {
  width: 100%;
}

.log-row td {
  vertical-align: middle;
}
</style>

<i18n>
{
  "en": {
    "title": "Log",
    "crumbs": "Attempts to open the lock, newest first",
    "request_failed": "The device does not answer: the log below may be out of date",
    "request_failed_no_data": "Could not get the access control state from the device",

    "attempts": "Opening attempts",
    "no_entries": "No opening attempts logged yet",
    "col_time": "Time",
    "col_user": "User",
    "col_source": "Source",
    "col_result": "Result",
    "source_fingerprint": "Fingerprint",
    "source_telegram": "Telegram",
    "source_mqtt": "MQTT",
    "source_chat": "{source} · {chat}",
    "result_granted": "Granted",
    "result_denied": "Denied",
    "result_unknown": "Not recognized"
  },
  "ru": {
    "title": "Журнал",
    "crumbs": "Попытки открыть замок, сначала новые",
    "request_failed": "Устройство не отвечает: журнал ниже может быть устаревшим",
    "request_failed_no_data": "Не удалось получить от устройства состояние контроля доступа",

    "attempts": "Попытки открытия",
    "no_entries": "Попыток открытия пока не было",
    "col_time": "Время",
    "col_user": "Пользователь",
    "col_source": "Источник",
    "col_result": "Результат",
    "source_fingerprint": "Отпечаток",
    "source_telegram": "Telegram",
    "source_mqtt": "MQTT",
    "source_chat": "{source} · {chat}",
    "result_granted": "Разрешён",
    "result_denied": "Запрещён",
    "result_unknown": "Не распознан"
  },
  "kk": {
    "title": "Журнал",
    "crumbs": "Құлыпты ашу әрекеттері, алдымен жаңалары",
    "request_failed": "Құрылғы жауап бермейді: төмендегі журнал ескірген болуы мүмкін",
    "request_failed_no_data": "Құрылғыдан қолжетімділікті бақылау күйін алу мүмкін болмады",

    "attempts": "Ашу әрекеттері",
    "no_entries": "Әзірге ашу әрекеттері болған жоқ",
    "col_time": "Уақыт",
    "col_user": "Пайдаланушы",
    "col_source": "Көзі",
    "col_result": "Нәтиже",
    "source_fingerprint": "Саусақ ізі",
    "source_telegram": "Telegram",
    "source_mqtt": "MQTT",
    "source_chat": "{source} · {chat}",
    "result_granted": "Рұқсат етілді",
    "result_denied": "Тыйым салынды",
    "result_unknown": "Танылмады"
  },
  "it": {
    "title": "Registro",
    "crumbs": "Tentativi di apertura della serratura, dal più recente",
    "request_failed": "Il dispositivo non risponde: il registro qui sotto potrebbe non essere aggiornato",
    "request_failed_no_data": "Impossibile ottenere dal dispositivo lo stato del controllo accessi",

    "attempts": "Tentativi di apertura",
    "no_entries": "Ancora nessun tentativo di apertura registrato",
    "col_time": "Ora",
    "col_user": "Utente",
    "col_source": "Origine",
    "col_result": "Esito",
    "source_fingerprint": "Impronta",
    "source_telegram": "Telegram",
    "source_mqtt": "MQTT",
    "source_chat": "{source} · {chat}",
    "result_granted": "Consentito",
    "result_denied": "Negato",
    "result_unknown": "Non riconosciuto"
  },
  "de": {
    "title": "Protokoll",
    "crumbs": "Versuche, das Schloss zu öffnen, die neuesten zuerst",
    "request_failed": "Das Gerät antwortet nicht: Das Protokoll unten ist möglicherweise veraltet",
    "request_failed_no_data": "Der Zustand der Zugangskontrolle konnte nicht vom Gerät abgerufen werden",

    "attempts": "Öffnungsversuche",
    "no_entries": "Noch keine Öffnungsversuche protokolliert",
    "col_time": "Zeit",
    "col_user": "Benutzer",
    "col_source": "Quelle",
    "col_result": "Ergebnis",
    "source_fingerprint": "Fingerabdruck",
    "source_telegram": "Telegram",
    "source_mqtt": "MQTT",
    "source_chat": "{source} · {chat}",
    "result_granted": "Gewährt",
    "result_denied": "Verweigert",
    "result_unknown": "Nicht erkannt"
  }
}
</i18n>
