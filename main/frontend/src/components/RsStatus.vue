<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import { RsSettings, RsStatus } from '@/common/types';
import Info from '@/components/Info.vue';

defineProps<{ title: string; info: RsStatus; settings: RsSettings }>();

const { t } = useI18n();
</script>

<template>
  <div class="rsStatus-label"><b>{{ title }}</b></div>
  <div></div>

  <div class="rsStatus-label">{{ t('modbus_mode') }}</div>
  <div class="rsStatus-value">{{ settings.bridge.modbus ? t('bridge_modbus') : t('bridge_transparent') }}</div>

  <div class="rsStatus-label">{{ t('bridge_mode') }}</div>
  <div class="rsStatus-value">{{ settings.bridge.mode === 'client' ? t('client') : t('server') }}</div>

  <div class="rsStatus-label">{{ t('tcp_port') }}</div>
  <div class="rsStatus-value">{{ settings.bridge.port }}</div>

  <div class="rsStatus-label">{{ t('tcp_count') }}</div>
  <div class="rsStatus-value">{{ info.server_connections_count }}</div>

  <div class="rsStatus-label">{{ t('status') }}</div>
  <div class="rsStatus-value">{{ info.is_busy ? t('exchange') : t('no_exchange') }}</div>
  <Info class="rsStatus-info" :text="t('status_info')" />

  <template v-if="settings.bridge.modbus ">
    <div class="rsStatus-label">{{ t('error_rate') }}</div>
    <div class="rsStatus-value">{{ info.error_percentage }} %</div>
  </template>
</template>

<style>
.rsStatus-label {
  justify-self: start !important;
}
.rsStatus-value {
  justify-self: end !important;
}

.rsStatus-info {
  margin-top: -14px;
}
</style>

<i18n>
{
  "en": {
    "modbus_mode": "Modbus mode",
    "bridge_mode": "Bridge mode",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent",
    "tcp_port": "TCP port",
    "tcp_count": "TCP count",
    "status": "Status",
    "status_info": "Enabled if there has been an exchange within the last 5 seconds",
    "exchange": "Has exchange",
    "no_exchange": "No exchange",
    "error_rate": "Error rate"
  },
  "ru": {
    "modbus_mode": "Режим",
    "bridge_mode": "Роль",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Прозрачный",
    "tcp_port": "TCP-порт",
    "tcp_count": "TCP подключений",
    "status": "Статус",
    "status_info": "Активен, если был обмен в течение последних 5 секунд",
    "exchange": "Есть обмен",
    "no_exchange": "Нет обмена",
    "error_rate": "Процент ошибок"
  }
}
</i18n>
