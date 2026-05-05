<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { changeLang, languages, type Locale } from '@/i18n';
import SaveIcon from '@/assets/save.svg?component';
import { useAlerts } from '@/common/alert';
import { useFirmware } from '@/common/firmware';
import { useInfo } from '@/common/info';
import { documentation, email, support, website, firmwareLatest, firmwareLatestVersion } from '@/common/links';
import { useSettings } from '@/common/settings';
import { useUptime } from '@/common/uptime';
import Switch from '@/components/Switch.vue';
import type { CommandResponse } from '@/common/types';
import Button from '@/components/Button.vue';
import Configuration from '@/components/Configuration.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InfoRow from '@/components/InfoRow.vue';
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

const latestVersion = ref<string | null>(null);
const latestVersionError = ref(false);

onMounted(async () => {
  try {
    const res = await fetch(firmwareLatestVersion);
    if (!res.ok) throw new Error();
    latestVersion.value = (await res.text()).trim();
  } catch {
    latestVersionError.value = true;
  }
});

const hasUpdate = computed(() =>
  latestVersion.value && info.value?.firmware && latestVersion.value !== info.value.firmware
);

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
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <div class="grid-2">
      <div class="stack">
        <section class="card">
          <form autocomplete="off" @submit.prevent="updateSettings({ hostname: settings!.hostname })">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">{{ t('device_name') }}</div>
                <div class="sub">{{ t('device_name_sub') }}</div>
              </div>
              <Button
                type="submit"
                :disabled="!settings!.hostname || !isChanged(['hostname'])"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="hostname">{{ t('hostname_label') }}</label>
                <input id="hostname" v-model="settings!.hostname" type="text" class="mono" name="hostname">
              </div>
              <InfoRow :label="t('access_url_label')">
                <a class="mono muted" :href="`http://${settings!.hostname}.local`" target="_blank">http://{{ settings!.hostname }}.local</a>
              </InfoRow>
            </div>
          </form>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('device_info') }}</div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('serial_num')"><span class="mono">{{ info!.serial_num }}</span></InfoRow>
            <InfoRow :label="t('uptime')">
              <span class="muted uptime-value">
                <template v-if="uptime">
                  <template v-if="uptime.days">
                    <span>{{ t('uptime_days', { n: uptime.days }) }}</span>
                  </template>
                  <template v-if="uptime.hours">
                    <span>{{ t('uptime_hours', { n: uptime.hours }) }}</span>
                  </template>
                  <span v-if="uptime.minutes > 0 || (!uptime.days && !uptime.hours && !uptime.seconds)">{{ t('uptime_minutes', { n: uptime.minutes }) }}</span>
                  <span v-if="uptime.seconds > 0 || (!uptime.days && !uptime.hours && !uptime.minutes)">{{ t('uptime_seconds', { n: uptime.seconds }) }}</span>
                </template>
                <template v-else>
                  <span>—</span>
                </template>
              </span>
            </InfoRow>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="card-title-wrap">
              <div class="title">{{ t('power_title') }}</div>
              <div class="sub">{{ t('power_sub') }}</div>
            </div>
            <div class="power-switch-row">
              <span class="power-label">V<sub>out</sub></span>
              <Switch
                id="system_vout"
                v-model="settings!.vout"
                @change="() => updateSettings({ vout: settings!.vout })"
              />
            </div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('power')"><span class="mono">{{ Number(info?.system_voltage.toFixed(1)) }} {{ t('v') }}</span></InfoRow>
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
        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('firmware') }}</div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('firmware_current')">
              <span class="firmware-version-row">
                <span>
                  <span class="mono">{{ info?.firmware }}</span>
                  <template v-if="latestVersion && !latestVersionError">
                    <span class="muted firmware-hint">({{ t('firmware_latest_label') }} <span class="mono">{{ latestVersion }}</span>)</span>
                  </template>
                  <span v-else-if="latestVersionError" class="muted firmware-hint">({{ t('firmware_check_failed') }})</span>
                </span>
                <a v-if="hasUpdate" :href="firmwareLatest" class="firmware-download-btn">
                  <Button type="button" variant="primary">{{ t('firmware_download', { v: latestVersion }) }}</Button>
                </a>
              </span>
            </InfoRow>
            <InfoRow :label="t('firmware_install')">
              <FileUpload
                v-model="firmwareFile"
                :placeholder="t('choose_firmware')"
                accept=".bin"
                :uploading-placeholder="isUpdating ? t('firmware_updating') : t('update')"
                :is-loading="isUpdating"
                :disabled="loadedMethod === 'firmware'"
                @upload="updateFirmware"
              />
              <template #hint>
                <Info v-if="firmwareFile" :text="t('wirmware_update_info')" />
              </template>
            </InfoRow>
            <InfoRow :label="t('reboot')">
              <Button type="button" variant="danger" :disabled="loadedMethod === 'reboot'" @click="cmd('reboot')">{{ t('restart') }}</Button>
            </InfoRow>
          </div>
        </section>

        <Configuration :cmd="cmd" :loaded-method="loadedMethod" />
      </div>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
.firmware-version-row {
  display: flex;
  align-items: center;
  gap: 10px;
  justify-content: flex-end;
  flex-wrap: wrap;
}

.firmware-download-btn {
  text-decoration: none;
}

.uptime-value {
  display: flex;
  gap: 4px;
}

/* Vout switch row in power card header */
.power-switch-row {
  display: flex;
  align-items: center;
  gap: 10px;
}

/* Vout label in power card header */
.power-label {
  font-size: 12px;
  color: var(--text-secondary);
}

</style>

<i18n>
{
  "en": {
    "title": "System",
    "crumbs": "Device & maintenance",
    "device_name": "Device name",
    "device_name_sub": "Used as hostname and mDNS name on the local network",
    "device_info": "Device information",
    "firmware": "Firmware",
    "hostname_label": "Name",
    "access_url_label": "Access URL",
    "serial_num": "Serial number",
    "power_title": "Power",
    "power_sub": "Supply & auxiliary output",
    "power": "Supply voltage",
    "v": "V",
    "uptime": "Uptime",
    "uptime_days": "- | {n} day | {n} days | {n} days",
    "uptime_hours": "less than an hour | {n} hour | {n} hours | {n} hours",
    "uptime_minutes": "minute | {n} minute | {n} minutes | {n} minutes",
    "uptime_seconds": "second | {n} second | {n} seconds | {n} seconds",
    "interface": "Web interface",
    "language": "Language",
    "firmware_current": "Current version",
    "firmware_latest_label": "latest",
    "firmware_check_failed": "update check unavailable",
    "firmware_download": "Download {v}",
    "firmware_install": "Install from file",
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
    "crumbs": "Устройство и обслуживание",
    "device_name": "Имя устройства",
    "device_name_sub": "Используется как hostname и mDNS-имя в локальной сети",
    "device_info": "Информация об устройстве",
    "firmware": "Прошивка",
    "hostname_label": "Имя",
    "access_url_label": "mDNS адрес",
    "serial_num": "Серийный номер",
    "power_title": "Питание",
    "power_sub": "Питание и вспомогательный выход",
    "power": "Напряжение питания",
    "v": "В",
    "uptime": "Время работы",
    "uptime_days": "- | {n} день | {n} дня | {n} дней",
    "uptime_hours": "- | {n} час | {n} часа | {n} часов",
    "uptime_minutes": "минута | {n} минута | {n} минуты | {n} минут",
    "uptime_seconds": "секунда | {n} секунда | {n} секунды | {n} секунд",
    "interface": "Веб-интерфейс",
    "language": "Язык",
    "firmware_current": "Текущая версия",
    "firmware_latest_label": "последняя",
    "firmware_check_failed": "проверка обновлений недоступна",
    "firmware_download": "Скачать {v}",
    "firmware_install": "Установить из файла",
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
    "crumbs": "Құрылғы және қызмет көрсету",
    "device_name": "Құрылғы атауы",
    "device_name_sub": "Жергілікті желіде hostname және mDNS атауы ретінде қолданылады",
    "device_info": "Құрылғы туралы ақпарат",
    "firmware": "Микробағдарлама",
    "hostname_label": "Атауы",
    "access_url_label": "Қол жеткізу URL",
    "serial_num": "Сериялық нөмір",
    "power_title": "Қуат",
    "power_sub": "Қуат және көмекші шығыс",
    "power": "Қорек кернеуі",
    "v": "В",
    "uptime": "Жұмыс уақыты",
    "uptime_days": "- | {n} күн | {n} күн | {n} күн",
    "uptime_hours": "бір сағаттан аз | {n} сағат | {n} сағат | {n} сағат",
    "uptime_minutes": "минут | {n} минут | {n} минут | {n} минут",
    "uptime_seconds": "секунд | {n} секунд | {n} секунд | {n} секунд",
    "interface": "Веб-интерфейс",
    "language": "Тіл",
    "firmware_current": "Ағымдағы нұсқа",
    "firmware_latest_label": "соңғы",
    "firmware_check_failed": "жаңарту тексерісі қолжетімсіз",
    "firmware_download": "{v} жүктеу",
    "firmware_install": "Файлдан орнату",
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
    "crumbs": "Dispositivo e manutenzione",
    "device_name": "Nome dispositivo",
    "device_name_sub": "Usato come hostname e nome mDNS nella rete locale",
    "device_info": "Informazioni dispositivo",
    "firmware": "Firmware",
    "hostname_label": "Nome",
    "access_url_label": "URL di accesso",
    "serial_num": "Numero di serie",
    "power_title": "Alimentazione",
    "power_sub": "Alimentazione e uscita ausiliaria",
    "power": "Tensione di alimentazione",
    "v": "V",
    "uptime": "Tempo di attività",
    "uptime_days": "- | {n} giorno | {n} giorni | {n} giorni",
    "uptime_hours": "meno di un'ora | {n} ora | {n} ore | {n} ore",
    "uptime_minutes": "minuto | {n} minuto | {n} minuti | {n} minuti",
    "uptime_seconds": "secondo | {n} secondo | {n} secondi | {n} secondi",
    "interface": "Interfaccia web",
    "language": "Lingua",
    "firmware_current": "Versione attuale",
    "firmware_latest_label": "ultima",
    "firmware_check_failed": "controllo aggiornamenti non disponibile",
    "firmware_download": "Scarica {v}",
    "firmware_install": "Installa da file",
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
    "crumbs": "Gerät und Wartung",
    "device_name": "Gerätename",
    "device_name_sub": "Wird als Hostname und mDNS-Name im lokalen Netzwerk verwendet",
    "device_info": "Geräteinformationen",
    "firmware": "Firmware",
    "hostname_label": "Name",
    "access_url_label": "Zugriffs-URL",
    "serial_num": "Seriennummer",
    "power_title": "Stromversorgung",
    "power_sub": "Versorgung und Hilfsausgang",
    "power": "Versorgungsspannung",
    "v": "V",
    "uptime": "Betriebszeit",
    "uptime_days": "- | {n} Tag | {n} Tage | {n} Tage",
    "uptime_hours": "weniger als eine Stunde | {n} Stunde | {n} Stunden | {n} Stunden",
    "uptime_minutes": "Minute | {n} Minute | {n} Minuten | {n} Minuten",
    "uptime_seconds": "Sekunde | {n} Sekunde | {n} Sekunden | {n} Sekunden",
    "interface": "Weboberfläche",
    "language": "Sprache",
    "firmware_current": "Aktuelle Version",
    "firmware_latest_label": "neueste",
    "firmware_check_failed": "Update-Prüfung nicht verfügbar",
    "firmware_download": "{v} herunterladen",
    "firmware_install": "Aus Datei installieren",
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
