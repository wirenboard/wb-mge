<script setup lang="ts">
import { ref } from 'vue';
import { useI18n } from 'vue-i18n';
import EyeIcon from '@/assets/eye.svg?component';
import EyeOffIcon from '@/assets/eyeOff.svg?component';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import InfoRow from '@/components/InfoRow.vue';
import InputNumber from '@/components/InputNumber.vue';
import Layout from '@/components/Layout.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
const { info } = useInfo();

// Toggle visibility of password fields
const showDeviceAuth = ref(false);
const showUserPass = ref(false);

const save = () => {
  updateSettings({
    knx: {
      enabled: data.value!.knx.enabled,
      port: data.value!.knx.port,
      device_auth: data.value!.knx.device_auth,
      user_pass: data.value!.knx.user_pass,
    },
  });
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">

        <!-- KNX Status card — shown only when the device reports KNX info -->
        <section v-if="info?.knx" class="card">
          <div class="card-header">
            <div class="title">{{ t('status_title') }}</div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('status_running')">
              <span :class="info.knx.running ? 'knx-status-ok' : 'knx-status-off'">
                {{ info.knx.running ? t('yes') : t('no') }}
              </span>
            </InfoRow>
            <InfoRow :label="t('status_bus_alive')">
              <span :class="info.knx.bus_alive ? 'knx-status-ok' : 'knx-status-off'">
                {{ info.knx.bus_alive ? t('yes') : t('no') }}
              </span>
            </InfoRow>
            <InfoRow :label="t('status_tcp_port')">
              <span class="mono">{{ info.knx.tcp_port }}</span>
            </InfoRow>
            <InfoRow :label="t('status_tx_count')">
              <span class="mono">{{ info.knx.tx_count }}</span>
            </InfoRow>
            <InfoRow :label="t('status_rx_count')">
              <span class="mono">{{ info.knx.rx_count }}</span>
            </InfoRow>
            <InfoRow :label="t('status_clients')">
              <span class="mono">{{ info.knx.clients_count }}</span>
            </InfoRow>
            <InfoRow :label="t('status_secure')">
              <span class="mono">{{ info.knx.secure_count }}</span>
            </InfoRow>
          </div>
        </section>

        <!-- KNX Settings card -->
        <section class="card">
          <form @submit.prevent="save">
            <div class="card-header">
              <div class="title">{{ t('settings_title') }}</div>
              <Button
                type="submit"
                :is-loading="isLoading && isChanged(['knx'])"
                :disabled="isLoading || !isChanged(['knx'])"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <!-- Enable/disable KNX -->
              <div class="field">
                <label for="knx_enabled">{{ t('enabled') }}</label>
                <div class="switch-end">
                  <Switch id="knx_enabled" v-model="data.knx.enabled" />
                </div>
              </div>

              <!-- Additional settings shown only when KNX is enabled -->
              <template v-if="data.knx.enabled">
                <!-- TCP port -->
                <div class="field">
                  <label for="knx_port">{{ t('port') }}</label>
                  <InputNumber
                    id="knx_port"
                    v-model="data.knx.port"
                    name="knx_port"
                    min="1"
                    max="65535"
                    class="knx-port mono"
                    required
                  />
                </div>

                <!-- Device authentication password -->
                <div class="field">
                  <label for="knx_device_auth">{{ t('device_auth') }}</label>
                  <div class="knx-password">
                    <input
                      id="knx_device_auth"
                      v-model="data.knx.device_auth"
                      :type="showDeviceAuth ? 'text' : 'password'"
                      name="knx_device_auth"
                      autocomplete="new-password"
                      :placeholder="t('device_auth_placeholder')"
                    />
                    <button type="button" class="knx-eye-btn" @click="showDeviceAuth = !showDeviceAuth">
                      <EyeIcon v-if="!showDeviceAuth" />
                      <EyeOffIcon v-else />
                    </button>
                  </div>
                </div>

                <!-- User / commissioning password -->
                <div class="field">
                  <label for="knx_user_pass">{{ t('user_pass') }}</label>
                  <div class="knx-password">
                    <input
                      id="knx_user_pass"
                      v-model="data.knx.user_pass"
                      :type="showUserPass ? 'text' : 'password'"
                      name="knx_user_pass"
                      autocomplete="new-password"
                      :placeholder="t('user_pass_placeholder')"
                    />
                    <button type="button" class="knx-eye-btn" @click="showUserPass = !showUserPass">
                      <EyeIcon v-if="!showUserPass" />
                      <EyeOffIcon v-else />
                    </button>
                  </div>
                </div>
              </template>
            </div>
          </form>
        </section>

      </div>
    </div>
  </Layout>
</template>

<style scoped>
.knx-port {
  max-width: 85px;
  justify-self: end;
}

/* Password field with eye toggle button */
.knx-password {
  position: relative;
  width: 100%;
}

.knx-password input {
  padding-right: 34px;
}

.knx-eye-btn {
  all: unset;
  position: absolute;
  right: 8px;
  top: 50%;
  transform: translateY(-50%);
  cursor: pointer;
  opacity: 0.5;
  display: flex;
  align-items: center;
  line-height: 0;
}

.knx-eye-btn:hover {
  opacity: 1;
}

.knx-status-ok {
  color: var(--success);
  font-weight: 500;
}

.knx-status-off {
  color: var(--text-muted);
}
</style>

<i18n>
{
  "en": {
    "title": "KNX IP Secure",
    "crumbs": "KNX IP Secure configuration",
    "status_title": "KNX Status",
    "settings_title": "KNX Settings",
    "status_running": "Running",
    "status_bus_alive": "Bus alive",
    "status_tcp_port": "TCP port",
    "status_tx_count": "TX frames",
    "status_rx_count": "RX frames",
    "status_clients": "Connected clients",
    "status_secure": "Secure connections",
    "yes": "Yes",
    "no": "No",
    "enabled": "Enable KNX IP Secure",
    "port": "TCP port",
    "device_auth": "Device auth password",
    "user_pass": "User password",
    "device_auth_placeholder": "Device authentication",
    "user_pass_placeholder": "Commissioning password"
  },
  "ru": {
    "title": "KNX IP Secure",
    "crumbs": "Настройка KNX IP Secure",
    "status_title": "Статус KNX",
    "settings_title": "Настройки KNX",
    "status_running": "Запущен",
    "status_bus_alive": "Шина активна",
    "status_tcp_port": "TCP-порт",
    "status_tx_count": "Отправлено кадров",
    "status_rx_count": "Получено кадров",
    "status_clients": "Подключено клиентов",
    "status_secure": "Защищённых соединений",
    "yes": "Да",
    "no": "Нет",
    "enabled": "Включить KNX IP Secure",
    "port": "TCP-порт",
    "device_auth": "Пароль аутентификации",
    "user_pass": "Пароль пользователя",
    "device_auth_placeholder": "Аутентификация устройства",
    "user_pass_placeholder": "Пароль ввода в эксплуатацию"
  },
  "kk": {
    "title": "KNX IP Secure",
    "crumbs": "KNX IP Secure конфигурациясы",
    "status_title": "KNX күйі",
    "settings_title": "KNX баптаулары",
    "status_running": "Іске қосылған",
    "status_bus_alive": "Шина белсенді",
    "status_tcp_port": "TCP порты",
    "status_tx_count": "Жіберілген кадрлар",
    "status_rx_count": "Қабылданған кадрлар",
    "status_clients": "Қосылған клиенттер",
    "status_secure": "Қауіпсіз қосылымдар",
    "yes": "Иә",
    "no": "Жоқ",
    "enabled": "KNX IP Secure қосу",
    "port": "TCP порты",
    "device_auth": "Аутентификация құпиясөзі",
    "user_pass": "Пайдаланушы құпиясөзі",
    "device_auth_placeholder": "Құрылғы аутентификациясы",
    "user_pass_placeholder": "Іске қосу құпиясөзі"
  },
  "it": {
    "title": "KNX IP Secure",
    "crumbs": "Configurazione KNX IP Secure",
    "status_title": "Stato KNX",
    "settings_title": "Impostazioni KNX",
    "status_running": "In esecuzione",
    "status_bus_alive": "Bus attivo",
    "status_tcp_port": "Porta TCP",
    "status_tx_count": "Frame TX",
    "status_rx_count": "Frame RX",
    "status_clients": "Client connessi",
    "status_secure": "Connessioni sicure",
    "yes": "Sì",
    "no": "No",
    "enabled": "Abilita KNX IP Secure",
    "port": "Porta TCP",
    "device_auth": "Password autenticazione dispositivo",
    "user_pass": "Password utente",
    "device_auth_placeholder": "Autenticazione dispositivo",
    "user_pass_placeholder": "Password di messa in servizio"
  },
  "de": {
    "title": "KNX IP Secure",
    "crumbs": "KNX IP Secure Konfiguration",
    "status_title": "KNX-Status",
    "settings_title": "KNX-Einstellungen",
    "status_running": "Läuft",
    "status_bus_alive": "Bus aktiv",
    "status_tcp_port": "TCP-Port",
    "status_tx_count": "Gesendete Frames",
    "status_rx_count": "Empfangene Frames",
    "status_clients": "Verbundene Clients",
    "status_secure": "Sichere Verbindungen",
    "yes": "Ja",
    "no": "Nein",
    "enabled": "KNX IP Secure aktivieren",
    "port": "TCP-Port",
    "device_auth": "Geräte-Auth-Passwort",
    "user_pass": "Benutzerpasswort",
    "device_auth_placeholder": "Geräte-Authentifizierung",
    "user_pass_placeholder": "Inbetriebnahme-Passwort"
  }
}
</i18n>
