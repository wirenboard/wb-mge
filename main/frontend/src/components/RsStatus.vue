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
    <div class="kv">
      <div class="k">{{ t('port_mode_label') }}</div>
      <div class="v">{{ t(`port_mode_${info.port_mode}`, info.port_mode) }}</div>
    </div>

    <!-- TCP bridge specific rows -->
    <template v-if="info.port_mode === 'tcp_bridge'">
      <div class="kv">
        <div class="k">{{ t('modbus_mode') }}</div>
        <div class="v">{{ settings.bridge.modbus ? t('bridge_modbus') : t('bridge_transparent') }}</div>
      </div>
      <div class="kv">
        <div class="k">{{ t('bridge_mode') }}</div>
        <div class="v">{{ settings.bridge.mode === 'client' ? t('client') : t('server') }}</div>
      </div>
      <div class="kv">
        <div class="k">{{ t('tcp_port') }}</div>
        <div class="v mono">{{ settings.bridge.port }}</div>
      </div>
      <div class="kv">
        <div class="k">{{ t('tcp_count') }}</div>
        <div class="v">{{ info.server_connections_count }}</div>
      </div>
      <template v-if="settings.bridge.modbus">
        <div class="kv">
          <div class="k">{{ t('error_rate') }}</div>
          <div class="v">{{ info.error_percentage }}%</div>
          <div class="hint">{{ t('error_rate_description') }}</div>
        </div>
      </template>
    </template>

    <!-- always visible: bus activity status -->
    <div class="kv">
      <div class="k">{{ t('status') }}</div>
      <div class="v">{{ info.is_busy ? t('active') : t('not_active') }}</div>
      <div class="hint">{{ t('status_info') }}</div>
    </div>
  </div>
</template>

<style>
</style>

<i18n>
{
  "en": {
    "port_mode_label": "Operating mode",
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
