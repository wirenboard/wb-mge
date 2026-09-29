<script setup lang="ts">
import { onBeforeUnmount, onMounted, ref, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { useConfirm } from '@/common/confirm';
import {
  addFpTgChat,
  alertFpFailure,
  clearFpTgChats,
  fetchFpTgChats,
  removeFpTgChat,
  setFpLock,
  useFpStatus,
} from '@/common/fpAcs';
import { useSettings } from '@/common/settings';
import type { FpTgChat } from '@/common/types';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import InputNumber from '@/components/InputNumber.vue';
import Layout from '@/components/Layout.vue';
import PasswordInput from '@/components/PasswordInput.vue';
import Switch from '@/components/Switch.vue';
import { onCustomValidation } from '@/utils/validation';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
const { confirmDialog } = useConfirm();

const lockFields = ['lock_mode', 'lock_addr', 'lock_channel', 'lock_pulse_ms'];
const telegramFields = ['tg_token', 'tg_admins'];

// Each card posts its own keys and nothing else, the way every other settings card does.
const saveLock = () => updateSettings({
  lock_mode: data.value!.lock_mode,
  lock_addr: data.value!.lock_addr,
  lock_channel: data.value!.lock_channel,
  lock_pulse_ms: data.value!.lock_pulse_ms,
});

const saveTelegram = () => updateSettings({
  tg_token: data.value!.tg_token,
  tg_admins: data.value!.tg_admins,
});

const { status, startPolling, stopPolling } = useFpStatus(ref(false));

onMounted(startPolling);
onBeforeUnmount(stopPolling);

// lock.on of the last status, or the state just asked for until the next status replaces it. Bound
// to the status alone, the switch would keep a toggle the relay did not follow: the checkbox flips
// on the click, and a lock.on that has not changed never sets it back.
const relayOn = ref(false);
watch(status, (next) => {
  if (next) {
    relayOn.value = next.lock.on;
  }
});

const toggleRelay = async () => {
  const on = !relayOn.value;
  relayOn.value = on;
  try {
    await setFpLock({ on });
  } catch (err) {
    await alertFpFailure(err);
  }
};

const chats = ref<FpTgChat[]>([]);
const chatsBusy = ref(false);
const newChat = ref<FpTgChat>({ id: '', name: '' });

const loadChats = async () => {
  try {
    chats.value = (await fetchFpTgChats()).chats;
  } catch (err) {
    await alertFpFailure(err);
  }
};

onMounted(loadChats);

// Every change is followed by a re-read of the list, which shows the device's own view of the result.
const mutateChats = async (request: () => Promise<unknown>): Promise<boolean> => {
  chatsBusy.value = true;
  try {
    await request();
    return true;
  } catch (err) {
    await alertFpFailure(err);
    return false;
  } finally {
    chatsBusy.value = false;
    await loadChats();
  }
};

const addChat = async () => {
  if (await mutateChats(() => addFpTgChat(newChat.value.id, newChat.value.name))) {
    newChat.value = { id: '', name: '' };
  }
};

const removeChat = (chat: FpTgChat) => mutateChats(() => removeFpTgChat(chat.id));

const clearChats = async () => {
  if (await confirmDialog(t('clear_chats_confirm'), t('clear_chats'))) {
    mutateChats(clearFpTgChats);
  }
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <section class="card">
          <form @submit.prevent="saveLock">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">{{ t('relay') }}</div>
                <div class="sub">{{ t('relay_sub') }}</div>
              </div>
              <Button
                type="submit"
                :is-loading="isLoading && isChanged(lockFields)"
                :disabled="isLoading || !isChanged(lockFields)"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="lock_on">{{ t('relay_switch') }}</label>
                <div class="switch-end"><Switch id="lock_on" :model-value="relayOn" @update:model-value="toggleRelay" /></div>
              </div>
              <div class="field">
                <label for="lock_mode">{{ t('lock_mode') }}</label>
                <select id="lock_mode" v-model="data.lock_mode" name="lock_mode">
                  <option value="strike">{{ t('lock_mode_strike') }}</option>
                  <option value="magnet">{{ t('lock_mode_magnet') }}</option>
                </select>
              </div>
              <div class="field">
                <label for="lock_addr">{{ t('lock_addr') }}</label>
                <InputNumber id="lock_addr" v-model="data.lock_addr" name="lock_addr" class="mono" min="1" max="247" required />
              </div>
              <div class="field">
                <label for="lock_channel">{{ t('lock_channel') }}</label>
                <InputNumber id="lock_channel" v-model="data.lock_channel" name="lock_channel" class="mono" min="1" max="8" required />
              </div>
              <div class="field">
                <label for="lock_pulse_ms">{{ t('lock_pulse_ms') }}</label>
                <InputNumber
                  id="lock_pulse_ms"
                  v-model="data.lock_pulse_ms"
                  name="lock_pulse_ms"
                  class="mono"
                  min="100"
                  max="60000"
                  required
                />
              </div>
              <i18n-t keypath="relay_note" tag="p" class="lock-note">
                <template #page>
                  <RouterLink to="/settings">{{ t('relay_note_page') }}</RouterLink>
                </template>
              </i18n-t>
            </div>
          </form>
        </section>

        <section class="card">
          <form @submit.prevent="saveTelegram">
            <div class="card-header">
              <div class="title">{{ t('telegram') }}</div>
              <Button
                type="submit"
                :is-loading="isLoading && isChanged(telegramFields)"
                :disabled="isLoading || !isChanged(telegramFields)"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="tg_token">{{ t('tg_token') }}</label>
                <PasswordInput
                  id="tg_token"
                  v-model="data.tg_token"
                  name="tg_token"
                  class="mono"
                  autocomplete="off"
                  pattern="^[\x21-\x7E]{0,63}$"
                  maxlength="63"
                  @input="(ev: Event) => onCustomValidation(ev, t('wrong_tg_token_pattern'))"
                />
              </div>
              <div class="field">
                <label for="tg_admins">{{ t('tg_admins') }}</label>
                <input
                  id="tg_admins"
                  v-model="data.tg_admins"
                  type="text"
                  name="tg_admins"
                  class="mono"
                  pattern="^([0-9]+(,[0-9]+)*)?$"
                  maxlength="63"
                  :placeholder="t('tg_admins_placeholder')"
                  @input="(ev) => onCustomValidation(ev, t('wrong_tg_admins_pattern'))"
                />
              </div>
            </div>
          </form>

          <div class="card-body sub-section">
            <div class="lock-chatsHeader">
              <div class="sub-section-label">{{ t('chats') }}</div>
              <Button type="button" variant="danger" :disabled="chatsBusy || !chats.length" @click="clearChats">
                {{ t('clear_chats') }}
              </Button>
            </div>
            <p v-if="!chats.length" class="lock-note">{{ t('no_chats') }}</p>
            <div v-else class="lock-tableWrap">
              <table class="lock-table">
                <thead>
                  <tr>
                    <th>{{ t('chat_id') }}</th>
                    <th>{{ t('chat_name') }}</th>
                    <th />
                  </tr>
                </thead>
                <tbody>
                  <tr v-for="chat in chats" :key="chat.id" class="lock-row">
                    <td class="mono">{{ chat.id }}</td>
                    <td>{{ chat.name }}</td>
                    <td class="lock-cellEnd">
                      <Button type="button" variant="danger" :disabled="chatsBusy" @click="removeChat(chat)">
                        {{ t('remove') }}
                      </Button>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
            <form class="lock-addChat" @submit.prevent="addChat">
              <input
                v-model="newChat.id"
                type="text"
                name="chat_id"
                class="mono"
                pattern="^-?[0-9]{1,19}$"
                :aria-label="t('chat_id')"
                :placeholder="t('chat_id')"
                @input="(ev) => onCustomValidation(ev, t('wrong_chat_id_pattern'))"
              />
              <input
                v-model="newChat.name"
                type="text"
                name="chat_name"
                :aria-label="t('chat_name')"
                :placeholder="t('chat_name')"
              />
              <!-- Not `required` on the inputs: this row is always on screen, and an empty required
                   input is painted as invalid before anything has been typed into it. -->
              <Button type="submit" :disabled="chatsBusy || !newChat.id">{{ t('add_chat') }}</Button>
            </form>
          </div>
        </section>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
.lock-note {
  margin: 12px 0 4px;
  color: var(--text-muted);
}

.lock-chatsHeader {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 12px;
}

.lock-tableWrap {
  margin-top: 8px;
  overflow-x: auto;
}

.lock-table {
  width: 100%;
}

.lock-row td {
  vertical-align: middle;
}

.lock-cellEnd {
  text-align: right;
}

.lock-addChat {
  display: flex;
  gap: 6px;
  margin-top: 12px;
}
</style>

<i18n>
{
  "en": {
    "title": "Lock",
    "crumbs": "Lock relay and Telegram bot",

    "relay": "Lock relay",
    "relay_sub": "Wiren Board Modbus relay module",
    "relay_switch": "Relay",
    "lock_mode": "Lock type",
    "lock_mode_strike": "Strike: on = open",
    "lock_mode_magnet": "Magnet: on = locked",
    "lock_addr": "Modbus address",
    "lock_channel": "Relay channel",
    "lock_pulse_ms": "Pulse length, ms",
    "relay_note": "The relay module is connected to RS-485 port 1. The line parameters of that port are set on the {page} page.",
    "relay_note_page": "Serial ports",

    "telegram": "Telegram bot",
    "tg_token": "Bot token",
    "tg_admins": "Admin IDs",
    "tg_admins_placeholder": "Numeric user IDs, comma-separated",
    "wrong_tg_token_pattern": "Enter up to 63 characters without spaces",
    "wrong_tg_admins_pattern": "Enter numeric Telegram user IDs separated by commas, without spaces",

    "chats": "Allowed chats",
    "no_chats": "No allowed chats",
    "chat_id": "Chat ID",
    "chat_name": "Name",
    "add_chat": "Add",
    "remove": "Remove",
    "clear_chats": "Clear all",
    "clear_chats_confirm": "Remove all allowed chats?",
    "wrong_chat_id_pattern": "Enter a numeric chat ID; group chat IDs start with a minus"
  },
  "ru": {
    "title": "Замок",
    "crumbs": "Реле замка и Telegram-бот",

    "relay": "Реле замка",
    "relay_sub": "Модуль реле Wiren Board с Modbus",
    "relay_switch": "Реле",
    "lock_mode": "Тип замка",
    "lock_mode_strike": "Защёлка: вкл = открыто",
    "lock_mode_magnet": "Магнит: вкл = закрыто",
    "lock_addr": "Modbus-адрес",
    "lock_channel": "Канал реле",
    "lock_pulse_ms": "Длительность импульса, мс",
    "relay_note": "Модуль реле подключается к порту RS-485 1. Параметры линии этого порта задаются на странице «{page}».",
    "relay_note_page": "Последовательные порты",

    "telegram": "Telegram-бот",
    "tg_token": "Токен бота",
    "tg_admins": "ID администраторов",
    "tg_admins_placeholder": "Числовые ID пользователей через запятую",
    "wrong_tg_token_pattern": "Введите до 63 символов без пробелов",
    "wrong_tg_admins_pattern": "Введите числовые ID пользователей Telegram через запятую, без пробелов",

    "chats": "Разрешённые чаты",
    "no_chats": "Разрешённых чатов нет",
    "chat_id": "ID чата",
    "chat_name": "Название",
    "add_chat": "Добавить",
    "remove": "Удалить",
    "clear_chats": "Удалить все",
    "clear_chats_confirm": "Удалить все разрешённые чаты?",
    "wrong_chat_id_pattern": "Введите числовой ID чата; ID групповых чатов начинаются с минуса"
  },
  "kk": {
    "title": "Құлып",
    "crumbs": "Құлып релесі және Telegram-бот",

    "relay": "Құлып релесі",
    "relay_sub": "Wiren Board Modbus реле модулі",
    "relay_switch": "Реле",
    "lock_mode": "Құлып түрі",
    "lock_mode_strike": "Ілгек: қосулы = ашық",
    "lock_mode_magnet": "Магнит: қосулы = жабық",
    "lock_addr": "Modbus мекенжайы",
    "lock_channel": "Реле арнасы",
    "lock_pulse_ms": "Импульс ұзақтығы, мс",
    "relay_note": "Реле модулі RS-485 1-портына қосылады. Бұл порттың желі параметрлері «{page}» бетінде орнатылады.",
    "relay_note_page": "Сериялық порттар",

    "telegram": "Telegram-бот",
    "tg_token": "Бот токені",
    "tg_admins": "Әкімшілер ID",
    "tg_admins_placeholder": "Пайдаланушылардың сандық ID, үтір арқылы",
    "wrong_tg_token_pattern": "Бос орынсыз 63 таңбаға дейін енгізіңіз",
    "wrong_tg_admins_pattern": "Telegram пайдаланушыларының сандық ID-ін үтір арқылы, бос орынсыз енгізіңіз",

    "chats": "Рұқсат етілген чаттар",
    "no_chats": "Рұқсат етілген чаттар жоқ",
    "chat_id": "Чат ID",
    "chat_name": "Атауы",
    "add_chat": "Қосу",
    "remove": "Жою",
    "clear_chats": "Барлығын жою",
    "clear_chats_confirm": "Барлық рұқсат етілген чаттарды жою керек пе?",
    "wrong_chat_id_pattern": "Чаттың сандық ID-ін енгізіңіз; топтық чаттардың ID-і минустан басталады"
  },
  "it": {
    "title": "Serratura",
    "crumbs": "Relè della serratura e bot Telegram",

    "relay": "Relè della serratura",
    "relay_sub": "Modulo relè Modbus Wiren Board",
    "relay_switch": "Relè",
    "lock_mode": "Tipo di serratura",
    "lock_mode_strike": "Incontro: attivo = aperta",
    "lock_mode_magnet": "Magnete: attivo = chiusa",
    "lock_addr": "Indirizzo Modbus",
    "lock_channel": "Canale del relè",
    "lock_pulse_ms": "Durata dell'impulso, ms",
    "relay_note": "Il modulo relè è collegato alla porta RS-485 1. I parametri di linea di quella porta si impostano nella pagina {page}.",
    "relay_note_page": "Porte seriali",

    "telegram": "Bot Telegram",
    "tg_token": "Token del bot",
    "tg_admins": "ID degli amministratori",
    "tg_admins_placeholder": "ID numerici degli utenti, separati da virgole",
    "wrong_tg_token_pattern": "Inserisci fino a 63 caratteri senza spazi",
    "wrong_tg_admins_pattern": "Inserisci ID numerici di utenti Telegram separati da virgole, senza spazi",

    "chats": "Chat consentite",
    "no_chats": "Nessuna chat consentita",
    "chat_id": "ID chat",
    "chat_name": "Nome",
    "add_chat": "Aggiungi",
    "remove": "Rimuovi",
    "clear_chats": "Rimuovi tutte",
    "clear_chats_confirm": "Rimuovere tutte le chat consentite?",
    "wrong_chat_id_pattern": "Inserisci un ID chat numerico; gli ID delle chat di gruppo iniziano con un meno"
  },
  "de": {
    "title": "Schloss",
    "crumbs": "Schlossrelais und Telegram-Bot",

    "relay": "Schlossrelais",
    "relay_sub": "Wiren Board Modbus-Relaismodul",
    "relay_switch": "Relais",
    "lock_mode": "Schlosstyp",
    "lock_mode_strike": "Türöffner: an = offen",
    "lock_mode_magnet": "Magnet: an = verriegelt",
    "lock_addr": "Modbus-Adresse",
    "lock_channel": "Relaiskanal",
    "lock_pulse_ms": "Impulsdauer, ms",
    "relay_note": "Das Relaismodul ist an RS-485-Port 1 angeschlossen. Die Leitungsparameter dieses Ports werden auf der Seite „{page}“ eingestellt.",
    "relay_note_page": "Serielle Schnittstellen",

    "telegram": "Telegram-Bot",
    "tg_token": "Bot-Token",
    "tg_admins": "Admin-IDs",
    "tg_admins_placeholder": "Numerische Benutzer-IDs, durch Kommas getrennt",
    "wrong_tg_token_pattern": "Geben Sie bis zu 63 Zeichen ohne Leerzeichen ein",
    "wrong_tg_admins_pattern": "Geben Sie numerische Telegram-Benutzer-IDs durch Kommas getrennt und ohne Leerzeichen ein",

    "chats": "Erlaubte Chats",
    "no_chats": "Keine erlaubten Chats",
    "chat_id": "Chat-ID",
    "chat_name": "Name",
    "add_chat": "Hinzufügen",
    "remove": "Entfernen",
    "clear_chats": "Alle entfernen",
    "clear_chats_confirm": "Alle erlaubten Chats entfernen?",
    "wrong_chat_id_pattern": "Geben Sie eine numerische Chat-ID ein; IDs von Gruppenchats beginnen mit einem Minus"
  }
}
</i18n>
