<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';
import InputNumber from '@/components/InputNumber.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
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
                  <label for="mqtt_prefix">{{ t('prefix') }}</label>
                  <input
                    id="mqtt_prefix"
                    v-model="data.mqtt.prefix"
                    type="text"
                    name="mqtt_prefix"
                    :placeholder="t('prefix_placeholder')"
                  />
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
      </div>
    </div>
  </Layout>
</template>

<i18n>
{
  "en": {
    "title": "MQTT",
    "crumbs": "MQTT broker connection",
    "mqtt_settings": "MQTT",
    "enabled": "Enable MQTT",
    "host": "Broker host",
    "host_placeholder": "e.g. 192.168.1.100",
    "port": "Port",
    "prefix": "Topic prefix",
    "prefix_placeholder": "e.g. wb-mge",
    "username": "Username",
    "password": "Password",
    "optional": "Optional",
    "save": "Save"
  },
  "ru": {
    "title": "MQTT",
    "crumbs": "Подключение к MQTT-брокеру",
    "mqtt_settings": "MQTT",
    "enabled": "Включить MQTT",
    "host": "Хост брокера",
    "host_placeholder": "например 192.168.1.100",
    "port": "Порт",
    "prefix": "Префикс топиков",
    "prefix_placeholder": "например wb-mge",
    "username": "Имя пользователя",
    "password": "Пароль",
    "optional": "Необязательно",
    "save": "Сохранить"
  },
  "kk": {
    "title": "MQTT",
    "crumbs": "MQTT брокеріне қосылу",
    "mqtt_settings": "MQTT",
    "enabled": "MQTT қосу",
    "host": "Брокер хосты",
    "host_placeholder": "мысалы 192.168.1.100",
    "port": "Порт",
    "prefix": "Тақырып префиксі",
    "prefix_placeholder": "мысалы wb-mge",
    "username": "Пайдаланушы аты",
    "password": "Құпия сөз",
    "optional": "Міндетті емес",
    "save": "Сақтау"
  },
  "it": {
    "title": "MQTT",
    "crumbs": "Connessione al broker MQTT",
    "mqtt_settings": "MQTT",
    "enabled": "Abilita MQTT",
    "host": "Host broker",
    "host_placeholder": "es. 192.168.1.100",
    "port": "Porta",
    "prefix": "Prefisso topic",
    "prefix_placeholder": "es. wb-mge",
    "username": "Nome utente",
    "password": "Password",
    "optional": "Opzionale",
    "save": "Salva"
  },
  "de": {
    "title": "MQTT",
    "crumbs": "MQTT-Broker-Verbindung",
    "mqtt_settings": "MQTT",
    "enabled": "MQTT aktivieren",
    "host": "Broker-Host",
    "host_placeholder": "z.B. 192.168.1.100",
    "port": "Port",
    "prefix": "Topic-Präfix",
    "prefix_placeholder": "z.B. wb-mge",
    "username": "Benutzername",
    "password": "Passwort",
    "optional": "Optional",
    "save": "Speichern"
  }
}
</i18n>
