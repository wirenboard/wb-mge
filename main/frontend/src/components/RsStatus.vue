<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import type { RsSettings, RsStatus } from '@/common/types';
import InfoRow from '@/components/InfoRow.vue';

defineProps<{ title: string; info: RsStatus; settings: RsSettings }>();

const { t } = useI18n();
</script>

<template>
  <div>
    <!-- always visible: operating mode -->
    <InfoRow :label="t('port_mode_label')">{{ t(`port_mode_${info.port_mode}`, info.port_mode) }}</InfoRow>

    <!-- TCP bridge specific rows -->
    <template v-if="info.port_mode === 'tcp_bridge'">
      <InfoRow :label="t('modbus_mode')">{{ settings.bridge.modbus ? t('bridge_modbus') : t('bridge_transparent') }}</InfoRow>
      <InfoRow v-if="!settings.bridge.modbus" :label="t('bridge_mode')">{{ settings.bridge.mode === 'client' ? t('client') : t('server') }}</InfoRow>
      <InfoRow :label="t('tcp_port')"><span class="mono">{{ settings.bridge.port }}</span></InfoRow>
      <InfoRow :label="t('tcp_count')">{{ info.server_connections_count }}</InfoRow>
      <template v-if="settings.bridge.modbus">
        <InfoRow :label="t('error_rate')">
          {{ info.error_percentage }}%
          <template #hint>{{ t('error_rate_description') }}</template>
        </InfoRow>
      </template>
    </template>

    <!-- cache overlay status — independent of transport mode -->
    <InfoRow :label="t('cache_label')">{{ info.cache_enabled ? t('cache_on') : t('cache_off') }}</InfoRow>

    <!-- always visible: bus activity status -->
    <InfoRow :label="t('status')">
      {{ info.is_busy ? t('active') : t('not_active') }}
      <template #hint>{{ t('status_info') }}</template>
    </InfoRow>
  </div>
</template>

<style>
</style>

<i18n>
{
  "en": {
    "port_mode_label": "Operating mode",
    "port_mode_disabled": "Disabled",
    "port_mode_tcp_bridge": "TCP bridge",
    "port_mode_passive": "Passive listen",
    "cache_label": "Cache",
    "cache_on": "Enabled",
    "cache_off": "Disabled",
    "modbus_mode": "Modbus mode",
    "bridge_mode": "Bridge mode",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparent bridge",
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
    "port_mode_label": "Режим работы",
    "port_mode_disabled": "Отключён",
    "port_mode_tcp_bridge": "TCP-мост",
    "port_mode_passive": "Пассивный (прослушка)",
    "cache_label": "Кэш",
    "cache_on": "Включён",
    "cache_off": "Выключен",
    "modbus_mode": "Режим",
    "bridge_mode": "Роль",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Прозрачный мост",
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
    "port_mode_label": "Жұмыс режимі",
    "port_mode_disabled": "Өшірілген",
    "port_mode_tcp_bridge": "TCP көпір",
    "port_mode_passive": "Пассивті тыңдау",
    "cache_label": "Кэш",
    "cache_on": "Қосулы",
    "cache_off": "Өшірулі",
    "modbus_mode": "Режим",
    "bridge_mode": "Рөл",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Мөлдір көпір",
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
    "port_mode_label": "Modalità operativa",
    "port_mode_disabled": "Disabilitato",
    "port_mode_tcp_bridge": "Bridge TCP",
    "port_mode_passive": "Ascolto passivo",
    "cache_label": "Cache",
    "cache_on": "Abilitata",
    "cache_off": "Disabilitata",
    "modbus_mode": "Modalità Modbus",
    "bridge_mode": "Modalità bridge",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Bridge trasparente",
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
    "port_mode_label": "Betriebsmodus",
    "port_mode_disabled": "Deaktiviert",
    "port_mode_tcp_bridge": "TCP-Bridge",
    "port_mode_passive": "Passives Mithören",
    "cache_label": "Cache",
    "cache_on": "Aktiviert",
    "cache_off": "Deaktiviert",
    "modbus_mode": "Modbus-Modus",
    "bridge_mode": "Bridge-Modus",
    "bridge_modbus": "Modbus TCP",
    "bridge_transparent": "Transparente Brücke",
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
