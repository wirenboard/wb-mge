<script setup lang="ts">
import { ref } from 'vue';
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
    const text = await templateFile.value[0].text();
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
    "template_uploaded_reboot": "Template uploaded. Reboot device to apply.",
    "template_upload_error": "Upload failed"
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
    "template_uploaded_reboot": "Шаблон загружен. Перезагрузите устройство для применения.",
    "template_upload_error": "Ошибка загрузки"
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
    "template_uploaded_reboot": "Шаблон жүктелді. Қолдану үшін қайта іске қосыңыз.",
    "template_upload_error": "Жүктеу қатесі"
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
    "mqts_slave_id": "Modbus slave ID"
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
    "mqts_slave_id": "Modbus slave ID"
  }
}
</i18n>
