<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import type { RsSettings, RsStatus } from '@/common/types';
import InfoRow from '@/components/InfoRow.vue';

defineProps<{ title: string; info: RsStatus; settings: RsSettings }>();

const { t } = useI18n();
</script>

<template>
  <div>
    <InfoRow :label="t('modbus_mode')">{{ settings.bridge.modbus ? t('bridge_modbus') : t('bridge_transparent') }}</InfoRow>
    <InfoRow :label="t('bridge_mode')">{{ settings.bridge.mode === 'client' ? t('client') : t('server') }}</InfoRow>
    <InfoRow :label="t('tcp_port')"><span class="mono">{{ settings.bridge.port }}</span></InfoRow>
    <InfoRow :label="t('tcp_count')">{{ info.server_connections_count }}</InfoRow>
    <InfoRow :label="t('status')">
      {{ info.is_busy ? t('active') : t('not_active') }}
      <template #hint>{{ t('status_info') }}</template>
    </InfoRow>
    <template v-if="settings.bridge.modbus">
      <InfoRow :label="t('error_rate')">
        {{ info.error_percentage }}%
        <template #hint>{{ t('error_rate_description') }}</template>
      </InfoRow>
    </template>
  </div>
</template>

<style>
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
  },
  "kk": {
    "modbus_mode": "Режим",
    "bridge_mode": "Рөл",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Мөлдір",
    "tcp_port": "TCP порты",
    "tcp_count": "TCP қосылымдары",
    "status": "Күйі",
    "status_info": "Соңғы 5 секундта дерек берілсе — белсенді",
    "active": "Белсенді",
    "not_active": "Белсенді емес",
    "error_rate": "Қате пайызы",
    "error_rate_description": "Соңғы 100 сұрауға қатысты қате пайызы"
  },
  "it": {
    "modbus_mode": "Modalità Modbus",
    "bridge_mode": "Modalità bridge",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Trasparente",
    "tcp_port": "Porta TCP",
    "tcp_count": "Connessioni TCP",
    "status": "Stato",
    "status_info": "Attivo se sono stati trasferiti dati negli ultimi 5 secondi",
    "active": "Attivo",
    "not_active": "Inattivo",
    "error_rate": "Tasso di errore",
    "error_rate_description": "Tasso di errore per le ultime 100 richieste"
  },
  "de": {
    "modbus_mode": "Modbus-Modus",
    "bridge_mode": "Bridge-Modus",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent",
    "tcp_port": "TCP-Port",
    "tcp_count": "TCP-Verbindungen",
    "status": "Status",
    "status_info": "Aktiv, wenn in den letzten 5 Sekunden Daten übertragen wurden",
    "active": "Aktiv",
    "not_active": "Inaktiv",
    "error_rate": "Fehlerquote",
    "error_rate_description": "Fehlerquote der letzten 100 Anfragen"
  }
}
</i18n>
