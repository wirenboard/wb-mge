<script setup lang="ts">
import { ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';
import InputNumber from '@/components/InputNumber.vue';
import FileUpload from '@/components/FileUpload.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();

const templateFile = ref<FileList | null>(null);
const isUploadingTemplate = ref(false);
const templateUploadResult = ref<string | null>(null);

const uploadTemplate = async () => {
  if (!templateFile.value?.length) return;
  isUploadingTemplate.value = true;
  templateUploadResult.value = null;
  try {
    const prefix = import.meta.env.DEV ? 'api/' : '';
    const rawText = await templateFile.value[0].text();
    /* Minify JSON before upload to reduce heap pressure on the device */
    const text = JSON.stringify(JSON.parse(rawText));
    const resp = await fetch(`${prefix}device-template`, {
      method: 'POST',
      body: text,
    });
    const json = await resp.json();
    if (json.success) {
      templateUploadResult.value = t('template_uploaded_reboot');
      templateFile.value = null;
    } else {
      templateUploadResult.value = json.error || t('template_upload_error');
    }
  } catch {
    templateUploadResult.value = t('template_upload_error');
  } finally {
    isUploadingTemplate.value = false;
  }
};

// GitHub template browser state
const githubTemplates = ref<Array<{ name: string; download_url: string }>>([]);
const isLoadingTemplates = ref(false);
const templatesLoadError = ref<string | null>(null);
const selectedTemplate = ref<string>('');         // stores download_url of selected template
const templateFilter = ref<string>('');           // search filter text
const isInstallingTemplate = ref(false);
const installResult = ref<string | null>(null);

// Computed filtered list
const filteredTemplates = computed(() => {
  if (!templateFilter.value) return githubTemplates.value;
  const q = templateFilter.value.toLowerCase();
  return githubTemplates.value.filter(tmpl => tmpl.name.toLowerCase().includes(q));
});

// Fetch list from GitHub API
const loadTemplates = async () => {
  isLoadingTemplates.value = true;
  templatesLoadError.value = null;
  githubTemplates.value = [];
  selectedTemplate.value = '';
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), 15000);
  try {
    const resp = await fetch(
      'https://api.github.com/repos/wirenboard/wb-mqtt-serial/contents/templates',
      { headers: { Accept: 'application/vnd.github.v3+json' }, signal: controller.signal }
    );
    if (resp.status === 403) throw new Error(t('github_rate_limit_error'));
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const data: Array<{ name: string; download_url: string; type: string }> = await resp.json();
    githubTemplates.value = data
      .filter(f => f.type === 'file' && f.name.endsWith('.json'))
      .map(f => ({ name: f.name.replace(/\.json$/, ''), download_url: f.download_url }))
      .sort((a, b) => a.name.localeCompare(b.name));
  } catch (e) {
    templatesLoadError.value = t('github_load_error');
  } finally {
    clearTimeout(timeoutId);
    isLoadingTemplates.value = false;
  }
};

// Install selected template
const installSelectedTemplate = async () => {
  if (!selectedTemplate.value) return;
  isInstallingTemplate.value = true;
  installResult.value = null;
  try {
    // Fetch the raw template JSON from GitHub with a 15-second timeout
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 15000);
    let text: string;
    try {
      const rawResp = await fetch(selectedTemplate.value, { signal: controller.signal });
      if (!rawResp.ok) throw new Error(`HTTP ${rawResp.status}`);
      /* Minify JSON before upload to reduce heap pressure on the device */
      text = JSON.stringify(JSON.parse(await rawResp.text()));
    } finally {
      clearTimeout(timeoutId);
    }

    // Upload to device via existing endpoint
    const prefix = import.meta.env.DEV ? 'api/' : '';
    const uploadResp = await fetch(`${prefix}device-template`, {
      method: 'POST',
      body: text,
    });
    const json = await uploadResp.json();
    if (json.success) {
      installResult.value = t('template_uploaded_reboot');
    } else {
      installResult.value = json.error || t('template_upload_error');
    }
  } catch {
    installResult.value = t('github_install_error');
  } finally {
    isInstallingTemplate.value = false;
  }
};

</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <section class="card">
          <form @submit.prevent="updateSettings({ mqtt: {
            enabled: data.mqtt.enabled,
            host: data.mqtt.host,
            port: data.mqtt.port,
            user: data.mqtt.user,
            pass: data.mqtt.pass,
            prefix: data.mqtt.prefix,
          } })">
            <div class="card-header">
              <div class="title">{{ t('mqtt_settings') }}</div>
              <Button
                type="submit"
                :is-loading="isLoading && isChanged(['mqtt'])"
                :disabled="isLoading || !isChanged(['mqtt'])"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="mqtt_enabled">{{ t('enabled') }}</label>
                <div style="justify-self: end"><Switch id="mqtt_enabled" v-model="data.mqtt.enabled" /></div>
              </div>

              <template v-if="data.mqtt.enabled">
                <div class="field">
                  <label for="mqtt_host">{{ t('host') }}</label>
                  <input
                    id="mqtt_host"
                    v-model="data.mqtt.host"
                    type="text"
                    name="mqtt_host"
                    :placeholder="t('host_placeholder')"
                    required
                  />
                </div>
                <div class="field">
                  <label for="mqtt_port">{{ t('port') }}</label>
                  <InputNumber id="mqtt_port" v-model="data.mqtt.port" name="mqtt_port" min="1" max="65535" required />
                </div>
                <div class="field">
                  <label for="mqtt_user">{{ t('username') }}</label>
                  <input
                    id="mqtt_user"
                    v-model="data.mqtt.user"
                    type="text"
                    name="mqtt_user"
                    :placeholder="t('optional')"
                    autocomplete="off"
                  />
                </div>
                <div class="field">
                  <label for="mqtt_pass">{{ t('password') }}</label>
                  <input
                    id="mqtt_pass"
                    v-model="data.mqtt.pass"
                    type="password"
                    name="mqtt_pass"
                    :placeholder="t('optional')"
                    autocomplete="new-password"
                  />
                </div>
              </template>
            </div>
          </form>
        </section>

        <section class="card">
          <form @submit.prevent="updateSettings({ mqts: {
            enabled: data.mqts.enabled,
            port: data.mqts.port,
            slave_id: data.mqts.slave_id,
          } })">
            <div class="card-header">
              <div class="title">{{ t('mqts_settings') }}</div>
              <Button
                type="submit"
                :is-loading="isLoading && isChanged(['mqts'])"
                :disabled="isLoading || !isChanged(['mqts'])"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="mqts_enabled">{{ t('mqts_enabled') }}</label>
                <div style="justify-self: end"><Switch id="mqts_enabled" v-model="data.mqts.enabled" /></div>
              </div>

              <template v-if="data.mqts.enabled">
                <div class="field">
                  <label for="mqts_port">{{ t('mqts_port') }}</label>
                  <select id="mqts_port" v-model="data.mqts.port" name="mqts_port">
                    <option :value="1">RS485-1</option>
                    <option :value="2">RS485-2</option>
                  </select>
                </div>
                <div class="field">
                  <label for="mqts_slave_id">{{ t('mqts_slave_id') }}</label>
                  <InputNumber id="mqts_slave_id" v-model="data.mqts.slave_id" name="mqts_slave_id" min="1" max="247" required />
                </div>
              </template>
            </div>
          </form>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('template_title') }}</div>
          </div>
          <div class="card-body">
            <div class="field">
              <label>{{ t('template_label') }}</label>
              <FileUpload
                v-model="templateFile"
                :placeholder="t('template_choose')"
                :uploading-placeholder="isUploadingTemplate ? t('template_uploading') : t('template_upload')"
                accept=".json"
                :is-loading="isUploadingTemplate"
                @upload="uploadTemplate"
              />
            </div>
            <div v-if="templateUploadResult" class="field">
              <span style="color: var(--color-text-secondary); font-size: 0.875rem">{{ templateUploadResult }}</span>
            </div>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('github_template_title') }}</div>
            <Button type="button" :is-loading="isLoadingTemplates" @click="loadTemplates">
              {{ githubTemplates.length ? t('reload') : t('load_from_github') }}
            </Button>
          </div>
          <div class="card-body">
            <div v-if="templatesLoadError" class="field">
              <span style="color: var(--color-danger, red); font-size: 0.875rem">{{ templatesLoadError }}</span>
            </div>
            <template v-if="githubTemplates.length">
              <div class="field">
                <label for="template_filter">{{ t('filter') }}</label>
                <input id="template_filter" v-model="templateFilter" type="text" :placeholder="t('filter_placeholder')" />
              </div>
              <div class="field">
                <label for="github_template_select">{{ t('select_template') }}</label>
                <select id="github_template_select" v-model="selectedTemplate">
                  <option value="" disabled>{{ t('choose_template') }}</option>
                  <option v-for="tmpl in filteredTemplates" :key="tmpl.download_url" :value="tmpl.download_url">
                    {{ tmpl.name }}
                  </option>
                </select>
              </div>
              <div class="field" style="justify-content: flex-end">
                <Button
                  type="button"
                  :is-loading="isInstallingTemplate"
                  :disabled="!selectedTemplate || isInstallingTemplate"
                  @click="installSelectedTemplate"
                >
                  {{ t('install_template') }}
                </Button>
              </div>
              <div v-if="installResult" class="field">
                <span style="color: var(--color-text-secondary); font-size: 0.875rem">{{ installResult }}</span>
              </div>
            </template>
          </div>
        </section>
      </div>
    </div>
  </Layout>
</template>

<i18n>
{
  "en": {
    "title": "Serial-MQTT Bridge",
    "crumbs": "Serial to MQTT bridge",
    "mqtt_settings": "MQTT broker",
    "enabled": "Enable MQTT",
    "host": "Broker host",
    "host_placeholder": "e.g. 192.168.1.100",
    "port": "Port",
    "prefix": "Topic prefix",
    "prefix_placeholder": "e.g. wb-mge",
    "username": "Username",
    "password": "Password",
    "optional": "Optional",
    "save": "Save",
    "mqts_settings": "MQTT serial bridge",
    "mqts_enabled": "Enable serial bridge",
    "mqts_port": "RS485 port",
    "mqts_slave_id": "Modbus slave ID",
    "template_title": "Device template",
    "template_label": "Template JSON file",
    "template_choose": "Choose file",
    "template_upload": "Upload",
    "template_uploading": "Uploading...",
    "template_uploaded_reboot": "Template uploaded. Bridge restarting...",
    "template_upload_error": "Upload failed",
    "github_template_title": "Install from GitHub",
    "load_from_github": "Load list",
    "reload": "Reload",
    "filter": "Filter",
    "filter_placeholder": "Type to filter...",
    "select_template": "Template",
    "choose_template": "Select a template...",
    "install_template": "Install",
    "github_load_error": "Failed to load template list from GitHub",
    "github_rate_limit_error": "Failed to load templates: GitHub API rate limit exceeded. Try again in an hour.",
    "github_install_error": "Failed to install template"
  },
  "ru": {
    "title": "Serial-MQTT Bridge",
    "crumbs": "Мост Serial–MQTT",
    "mqtt_settings": "MQTT брокер",
    "enabled": "Включить MQTT",
    "host": "Хост брокера",
    "host_placeholder": "например 192.168.1.100",
    "port": "Порт",
    "prefix": "Префикс топиков",
    "prefix_placeholder": "например wb-mge",
    "username": "Имя пользователя",
    "password": "Пароль",
    "optional": "Необязательно",
    "save": "Сохранить",
    "mqts_settings": "MQTT serial bridge",
    "mqts_enabled": "Включить serial bridge",
    "mqts_port": "Порт RS485",
    "mqts_slave_id": "Modbus slave ID",
    "template_title": "Шаблон устройства",
    "template_label": "JSON-файл шаблона",
    "template_choose": "Выбрать файл",
    "template_upload": "Загрузить",
    "template_uploading": "Загрузка...",
    "template_uploaded_reboot": "Шаблон загружен. Мост перезапускается...",
    "template_upload_error": "Ошибка загрузки",
    "github_template_title": "Установить с GitHub",
    "load_from_github": "Загрузить список",
    "reload": "Обновить",
    "filter": "Фильтр",
    "filter_placeholder": "Введите для фильтрации...",
    "select_template": "Шаблон",
    "choose_template": "Выберите шаблон...",
    "install_template": "Установить",
    "github_load_error": "Не удалось загрузить список шаблонов с GitHub",
    "github_rate_limit_error": "Не удалось загрузить шаблоны: превышен лимит GitHub API. Повторите через час.",
    "github_install_error": "Не удалось установить шаблон"
  },
  "kk": {
    "title": "Serial-MQTT Bridge",
    "crumbs": "Serial–MQTT көпірі",
    "mqtt_settings": "MQTT брокер",
    "enabled": "MQTT қосу",
    "host": "Брокер хосты",
    "host_placeholder": "мысалы 192.168.1.100",
    "port": "Порт",
    "prefix": "Тақырып префиксі",
    "prefix_placeholder": "мысалы wb-mge",
    "username": "Пайдаланушы аты",
    "password": "Құпия сөз",
    "optional": "Міндетті емес",
    "save": "Сақтау",
    "mqts_settings": "MQTT serial bridge",
    "mqts_enabled": "Serial bridge қосу",
    "mqts_port": "RS485 порты",
    "mqts_slave_id": "Modbus slave ID",
    "template_title": "Құрылғы шаблоны",
    "template_label": "JSON шаблон файлы",
    "template_choose": "Файл таңдау",
    "template_upload": "Жүктеу",
    "template_uploading": "Жүктелуде...",
    "template_uploaded_reboot": "Шаблон жүктелді. Көпір қайта іске қосылуда...",
    "template_upload_error": "Жүктеу қатесі",
    "github_template_title": "GitHub-тан орнату",
    "load_from_github": "Тізімді жүктеу",
    "reload": "Жаңарту",
    "filter": "Сүзгі",
    "filter_placeholder": "Іздеу...",
    "select_template": "Үлгі",
    "choose_template": "Үлгіні таңдаңыз...",
    "install_template": "Орнату",
    "github_load_error": "GitHub-тан үлгі тізімін жүктеу мүмкін болмады",
    "github_rate_limit_error": "Үлгілерді жүктеу мүмкін болмады: GitHub API сұраулар шегі асты. Бір сағаттан кейін қайталаңыз.",
    "github_install_error": "Үлгіні орнату мүмкін болмады"
  },
  "it": {
    "title": "Serial-MQTT Bridge",
    "crumbs": "Bridge Serial–MQTT",
    "mqtt_settings": "Broker MQTT",
    "enabled": "Abilita MQTT",
    "host": "Host broker",
    "host_placeholder": "es. 192.168.1.100",
    "port": "Porta",
    "prefix": "Prefisso topic",
    "prefix_placeholder": "es. wb-mge",
    "username": "Nome utente",
    "password": "Password",
    "optional": "Opzionale",
    "save": "Salva",
    "mqts_settings": "MQTT serial bridge",
    "mqts_enabled": "Abilita serial bridge",
    "mqts_port": "Porta RS485",
    "mqts_slave_id": "Modbus slave ID",
    "template_title": "Template dispositivo",
    "template_label": "File JSON template",
    "template_choose": "Scegli file",
    "template_upload": "Carica",
    "template_uploading": "Caricamento...",
    "template_uploaded_reboot": "Template caricato. Bridge in riavvio...",
    "template_upload_error": "Errore di caricamento",
    "github_template_title": "Installa da GitHub",
    "load_from_github": "Carica lista",
    "reload": "Ricarica",
    "filter": "Filtra",
    "filter_placeholder": "Digita per filtrare...",
    "select_template": "Template",
    "choose_template": "Seleziona un template...",
    "install_template": "Installa",
    "github_load_error": "Impossibile caricare la lista dei template da GitHub",
    "github_rate_limit_error": "Impossibile caricare i template: limite GitHub API raggiunto. Riprova tra un'ora.",
    "github_install_error": "Impossibile installare il template"
  },
  "de": {
    "title": "Serial-MQTT Bridge",
    "crumbs": "Serial-MQTT-Bridge",
    "mqtt_settings": "MQTT-Broker",
    "enabled": "MQTT aktivieren",
    "host": "Broker-Host",
    "host_placeholder": "z.B. 192.168.1.100",
    "port": "Port",
    "prefix": "Topic-Präfix",
    "prefix_placeholder": "z.B. wb-mge",
    "username": "Benutzername",
    "password": "Passwort",
    "optional": "Optional",
    "save": "Speichern",
    "mqts_settings": "MQTT serial bridge",
    "mqts_enabled": "Serial bridge aktivieren",
    "mqts_port": "RS485-Port",
    "mqts_slave_id": "Modbus slave ID",
    "template_title": "Ger\u00e4tevorlage",
    "template_label": "JSON-Vorlagendatei",
    "template_choose": "Datei ausw\u00e4hlen",
    "template_upload": "Hochladen",
    "template_uploading": "Hochladen...",
    "template_uploaded_reboot": "Vorlage hochgeladen. Bridge wird neu gestartet...",
    "template_upload_error": "Upload fehlgeschlagen",
    "github_template_title": "Von GitHub installieren",
    "load_from_github": "Liste laden",
    "reload": "Neu laden",
    "filter": "Filter",
    "filter_placeholder": "Zum Filtern eingeben...",
    "select_template": "Vorlage",
    "choose_template": "Vorlage auswählen...",
    "install_template": "Installieren",
    "github_load_error": "Vorlagenliste von GitHub konnte nicht geladen werden",
    "github_rate_limit_error": "Vorlagen konnten nicht geladen werden: GitHub API-Limit überschritten. Versuche es in einer Stunde erneut.",
    "github_install_error": "Vorlage konnte nicht installiert werden"
  }
}
</i18n>
