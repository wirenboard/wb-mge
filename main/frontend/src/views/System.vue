<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { changeLang, languages, type Locale } from '@/i18n';
import SaveIcon from '@/assets/save.svg?component';
import { useAlerts } from '@/common/alert';
import { useFirmware } from '@/common/firmware';
import { useInfo } from '@/common/info';
import { documentation, email, support, website, firmwareLatest } from '@/common/links';
import { useSettings } from '@/common/settings';
import { useUptime } from '@/common/uptime';
import type { CommandResponse } from '@/common/types';
import Button from '@/components/Button.vue';
import Configuration from '@/components/Configuration.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InputNumber from '@/components/InputNumber.vue';
import FileUpload from '@/components/FileUpload.vue';
import Layout from '@/components/Layout.vue';
import { api } from '@/utils/api';
import { onCustomValidation } from '@/utils/validation';

const { t, locale } = useI18n();
const language = ref<Locale>(locale.value as Locale);
const firmwareFile = ref<File[]>();
const loadedMethod = ref();
const { isUpdating, update } = useFirmware();
const { showAlert } = useAlerts();
const { data: settings, isChanged, updateSettings } = useSettings();
const { isReconnecting, uptime } = useUptime();
const { info } = useInfo();

const updateFirmware = async () => {
  loadedMethod.value = 'firmware';
  showAlert(t('firmware_update_processed'), { type: 'success' });

  try {
    await update(firmwareFile.value?.[0] as File);
    location.reload();
  } catch (err) {
    firmwareFile.value = [];
    showAlert(t('wirmware_update_error'), { type: 'error' });
  } finally {
    loadedMethod.value = null;
  }
};

const cmd = async (command: string, confirmText?: string) => {
  if (confirmText) {
    const isConfirm = confirm(confirmText);
    if (!isConfirm) {
      return;
    }
  }

  loadedMethod.value = command;
  await api<CommandResponse>('cmd', { method: 'POST', json: { cmd: command } });
  isReconnecting.value = true;
  loadedMethod.value = null;
  setTimeout(() => {
    location.reload();
  }, 3500);
};

const updateInterface = () => {
  if (isChanged(['login', 'pass', 'web_port'])) {
    updateSettings({
      login: settings.value!.login,
      pass: settings.value!.pass,
      web_port: settings.value!.web_port,
    });
  }
  changeLang(language.value);
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" />

    <div class="main-body">
    <div class="grid-2">
      <div class="stack">
        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('device') }}</div>
          </div>
          <div class="card-body">
            <div class="kv">
              <div class="k">{{ t('hostname') }}</div>
              <div class="v">
                <form class="system-saveWrapper" autocomplete="off" @submit.prevent="updateSettings({ hostname: settings!.hostname })">
                  <input v-model="settings!.hostname" type="text" name="hostname">
                  <button type="submit" :disabled="!settings!.hostname || !isChanged(['hostname'])">
                    <SaveIcon class="system-save" />
                  </button>
                </form>
              </div>
            </div>
            <div class="kv">
              <div class="k">{{ t('serial_num') }}</div>
              <div class="v">{{ info!.serial_num }}</div>
            </div>
            <template v-if="uptime">
              <div class="kv">
                <div class="k">{{ t('uptime') }}</div>
                <div class="v muted">
                  <template v-if="uptime.days">
                    <span class="system-uptime">{{ t('uptime_days', { n: uptime.days }) }}</span>
                  </template>
                  <template v-if="uptime.hours">
                    <span class="system-uptime">{{ t('uptime_hours', { n: uptime.hours }) }}</span>
                  </template>
                  <span>{{ t('uptime_minutes', { n: uptime.minutes }) }}</span>
                </div>
              </div>
            </template>
            <div class="kv">
              <div class="k">{{ t('firmware_version') }}</div>
              <div class="v mono">{{ info?.firmware }}</div>
            </div>
            <div class="kv">
              <div class="k">{{ t('firmware_update') }}</div>
              <div class="v">
                <FileUpload
                  v-model="firmwareFile"
                  :placeholder="t('choose_firmware')"
                  accept=".bin"
                  :uploading-placeholder="isUpdating ? t('firmware_updating') : t('update')"
                  :is-loading="isUpdating"
                  :disabled="loadedMethod === 'firmware'"
                  @upload="updateFirmware"
                />
              </div>
            </div>
            <Info v-if="firmwareFile" :text="t('wirmware_update_info')" />
            <div class="kv">
              <div class="k">{{ t('reboot') }}</div>
              <div class="v">
                <Button type="button" variant="danger" :disabled="loadedMethod === 'reboot'" @click="cmd('reboot')">{{ t('restart') }}</Button>
              </div>
            </div>
          </div>
        </section>

        <section class="card">
          <form
            :autocomplete="isChanged(['login', 'pass']) ? 'on' : 'off'"
            @submit.prevent="updateInterface">
            <div class="card-header">
              <div class="title">{{ t('interface') }}</div>
              <Button
                type="submit"
                :disabled="!isChanged(['login', 'pass', 'web_port']) && language === locale"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="port">{{ t('port') }}</label>
                <InputNumber id="port" v-model="settings!.web_port" type="text" name="port" min="1" max="65535" required />
              </div>
              <div class="field">
                <label for="username">{{ t('login') }}</label>
                <input
                  id="username"
                  v-model="settings!.login"
                  type="text"
                  pattern="^[a-zA-Z0-9_\-]+$"
                  name="username"
                  :autocomplete="isChanged(['login']) ? 'username' : 'off'"
                  required
                  @input="(ev) => onCustomValidation(ev, t('wrong_username_pattern'))"
                />
              </div>
              <div class="field">
                <label for="new-password">{{ t('password') }}</label>
                <input
                  id="new-password"
                  v-model="settings!.pass"
                  :placeholder="t('pass_placeholder')"
                  :autocomplete="isChanged(['pass']) ? 'new-password' : 'off'"
                  type="password"
                  name="new-password"
                  pattern="^[\x20-\x7E]+$"
                  required
                  @input="(ev) => onCustomValidation(ev, t('wrong_password_pattern'))"
                />
              </div>
              <div class="field">
                <label for="language">{{ t('language') }}</label>
                <select id="language" v-model="language" name="language">
                  <option v-for="(lang, code) in languages" :key="code" :value="code">{{ lang }}</option>
                </select>
              </div>
            </div>
          </form>
        </section>
      </div>

      <div class="stack">
        <Configuration :cmd="cmd" :loaded-method="loadedMethod" />

      </div>
    </div>
    </div>
  </Layout>
</template>

<style scoped>
.system-links {
  display: flex;
  flex-direction: column;
  gap: 10px;
  font-size: 13px;
}

.system-saveWrapper {
  display: flex;
  gap: 6px;
  justify-content: flex-end;
}

.system-save {
  width: 12px;
  height: 12px;
}

.system-uptime {
  margin-right: 4px;
}
</style>

<i18n>
{
  "en": {
    "title": "System",
    "device": "Device",
    "hostname": "Hostname",
    "serial_num": "Serial number",
    "uptime": "Uptime",
    "uptime_days": "- | {n} day | {n} days | {n} days",
    "uptime_hours": "less than an hour | {n} hour | {n} hours | {n} hours",
    "uptime_minutes": "minute | {n} minute | {n} minutes | {n} minutes",
    "interface": "Web interface",
    "language": "Language",
    "firmware_version": "Firmware version",
    "firmware_update": "Firmware update",
    "wirmware_update_info": "The device will reboot after the update",
    "firmware_update_processed": "Firmware update in progress",
    "wirmware_update_error": "Firmware update error",
    "choose_firmware": "Choose file",
    "update": "Update",
    "firmware_updating": "Updating",
    "reboot": "Reboot",
    "restart": "Reboot device",
    "links": "Links",
    "documentation": "Documentation",
    "support": "Support",
    "website": "Buy devices",
    "firmware_link": "Download latest firmware",
    "wrong_username_pattern": "Use only Latin letters, numbers, hyphens, and underscores",
    "wrong_password_pattern": "Use only Latin letters, numbers, spaces, and special characters"
  },
  "ru": {
    "title": "Система",
    "device": "Устройство",
    "hostname": "Название хоста",
    "serial_num": "Серийный номер",
    "uptime": "Время работы",
    "uptime_days": "- | {n} день | {n} дня | {n} дней",
    "uptime_hours": "- | {n} час | {n} часа | {n} часов",
    "uptime_minutes": "минута | {n} минута | {n} минуты | {n} минут",
    "interface": "Веб-интерфейс",
    "language": "Язык",
    "firmware_version": "Версия ПО",
    "firmware_update": "Обновление ПО",
    "wirmware_update_info": "После обновления устройство будет перезагружено",
    "firmware_update_processed": "Обновление ПО в процессе",
    "wirmware_update_error": "Ошибка обновления прошивки",
    "choose_firmware": "Выбрать файл",
    "update": "Обновить",
    "firmware_updating": "Обновление",
    "reboot": "Перезагрузка",
    "restart": "Перезагрузить",
    "links": "Ссылки",
    "documentation": "Документация",
    "support": "Техподдержка",
    "website": "Купить устройства",
    "firmware_link": "Скачать последнюю прошивку",
    "wrong_username_pattern": "Используйте только латиницу, цифры, дефисы и нижние подчеркивания",
    "wrong_password_pattern": "Используйте только латиницу, цифры, пробелы и спецсимволы"
  },
  "kk": {
    "title": "Жүйе",
    "device": "Құрылғы",
    "hostname": "Хост атауы",
    "serial_num": "Сериялық нөмір",
    "uptime": "Жұмыс уақыты",
    "uptime_days": "- | {n} күн | {n} күн | {n} күн",
    "uptime_hours": "бір сағаттан аз | {n} сағат | {n} сағат | {n} сағат",
    "uptime_minutes": "минут | {n} минут | {n} минут | {n} минут",
    "interface": "Веб-интерфейс",
    "language": "Тіл",
    "firmware_version": "Микробағдарлама нұсқасы",
    "firmware_update": "Микробағдарламаны жаңарту",
    "wirmware_update_info": "Жаңартудан кейін құрылғы қайта жүктеледі",
    "firmware_update_processed": "Микробағдарламаны жаңарту жүріп жатыр",
    "wirmware_update_error": "Микробағдарламаны жаңарту қатесі",
    "choose_firmware": "Файлды таңдаңыз",
    "update": "Жаңарту",
    "firmware_updating": "Жаңартылуда",
    "reboot": "Қайта жүктеу",
    "restart": "Құрылғыны қайта жүктеу",
    "links": "Сілтемелер",
    "documentation": "Құжаттама",
    "support": "Қолдау",
    "website": "Құрылғыларды сатып алу",
    "firmware_link": "Соңғы микробағдарламаны жүктеу",
    "wrong_username_pattern": "Тек латын әріптері, сандар, дефис және астыңғы сызықша қолданыңыз",
    "wrong_password_pattern": "Тек латын әріптері, сандар, бос орындар және арнайы таңбалар қолданыңыз"
  },
  "it": {
    "title": "Sistema",
    "device": "Dispositivo",
    "hostname": "Nome host",
    "serial_num": "Numero di serie",
    "uptime": "Tempo di attività",
    "uptime_days": "- | {n} giorno | {n} giorni | {n} giorni",
    "uptime_hours": "meno di un'ora | {n} ora | {n} ore | {n} ore",
    "uptime_minutes": "minuto | {n} minuto | {n} minuti | {n} minuti",
    "interface": "Interfaccia web",
    "language": "Lingua",
    "firmware_version": "Versione firmware",
    "firmware_update": "Aggiornamento firmware",
    "wirmware_update_info": "Il dispositivo si riavvierà dopo l'aggiornamento",
    "firmware_update_processed": "Aggiornamento firmware in corso",
    "wirmware_update_error": "Errore di aggiornamento firmware",
    "choose_firmware": "Scegli file",
    "update": "Aggiorna",
    "firmware_updating": "Aggiornamento",
    "reboot": "Riavvia",
    "restart": "Riavvia dispositivo",
    "links": "Link",
    "documentation": "Documentazione",
    "support": "Supporto",
    "website": "Acquista dispositivi",
    "firmware_link": "Scarica l'ultimo firmware",
    "wrong_username_pattern": "Usa solo lettere latine, numeri, trattini e underscore",
    "wrong_password_pattern": "Usa solo lettere latine, numeri, spazi e caratteri speciali"
  },
  "de": {
    "title": "System",
    "device": "Gerät",
    "hostname": "Hostname",
    "serial_num": "Seriennummer",
    "uptime": "Betriebszeit",
    "uptime_days": "- | {n} Tag | {n} Tage | {n} Tage",
    "uptime_hours": "weniger als eine Stunde | {n} Stunde | {n} Stunden | {n} Stunden",
    "uptime_minutes": "Minute | {n} Minute | {n} Minuten | {n} Minuten",
    "interface": "Weboberfläche",
    "language": "Sprache",
    "firmware_version": "Firmware-Version",
    "firmware_update": "Firmware-Update",
    "wirmware_update_info": "Das Gerät wird nach dem Update neu gestartet",
    "firmware_update_processed": "Firmware-Update läuft",
    "wirmware_update_error": "Fehler beim Firmware-Update",
    "choose_firmware": "Datei auswählen",
    "update": "Aktualisieren",
    "firmware_updating": "Wird aktualisiert",
    "reboot": "Neustart",
    "restart": "Gerät neu starten",
    "links": "Links",
    "documentation": "Dokumentation",
    "support": "Support",
    "website": "Geräte kaufen",
    "firmware_link": "Neueste Firmware herunterladen",
    "wrong_username_pattern": "Nur lateinische Buchstaben, Zahlen, Bindestriche und Unterstriche verwenden",
    "wrong_password_pattern": "Nur lateinische Buchstaben, Zahlen, Leerzeichen und Sonderzeichen verwenden"
  }
}
</i18n>
