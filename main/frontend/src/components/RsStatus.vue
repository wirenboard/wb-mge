<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import type { RsSettings, RsStatus } from '@/common/types';
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
  <div class="rsStatus-value">{{ info.is_busy ? t('active') : t('not_active') }}</div>
  <Info class="rsStatus-info" :text="t('status_info')" />

  <template v-if="settings.bridge.modbus ">
    <div class="rsStatus-label">{{ t('error_rate') }}</div>
    <div class="rsStatus-value">{{ info.error_percentage }}%</div>
    <Info class="rsStatus-info" :text="t('error_rate_description')" />
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
    "status_info": "Active if data has been transferred within the last 5 seconds",
    "active": "Active",
    "not_active": "Inactive",
    "error_rate": "Error rate",
    "error_rate_description": "Error rate for the last 100 requests"
  },
  "ru": {
    "modbus_mode": "Режим",
    "bridge_mode": "Роль",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Прозрачный",
    "tcp_port": "TCP-порт",
    "tcp_count": "TCP подключений",
    "status": "Статус",
    "status_info": "Активен, если была передача данных в течение последних 5 секунд",
    "active": "Активен",
    "not_active": "Не активен",
    "error_rate": "Процент ошибок",
    "error_rate_description": "Процент ошибок по последним 100 запросам"
  }
}
</i18n>
