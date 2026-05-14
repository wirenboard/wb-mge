<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import type { RsStatus } from '@/common/types';
import InfoRow from '@/components/InfoRow.vue';
import Info from '@/components/Info.vue';

defineProps<{ info: RsStatus }>();

const { t } = useI18n();
</script>

<template>
  <InfoRow :label="t('status')">
    {{ info.is_busy ? t('active') : t('not_active') }}
    <template #hint>
      <Info :text="t('status_info')" />
    </template>
  </InfoRow>
</template>

<i18n>
{
  "en": {
    "status": "Status",
    "status_info": "Active if data has been transferred within the last 5 seconds",
    "active": "Active",
    "not_active": "Inactive"
  },
  "ru": {
    "status": "Статус",
    "status_info": "Активен, если была передача данных в течение последних 5 секунд",
    "active": "Активен",
    "not_active": "Не активен"
  },
  "kk": {
    "status": "Күйі",
    "status_info": "Соңғы 5 секундта дерек берілсе — белсенді",
    "active": "Белсенді",
    "not_active": "Белсенді емес"
  },
  "it": {
    "status": "Stato",
    "status_info": "Attivo se sono stati trasferiti dati negli ultimi 5 secondi",
    "active": "Attivo",
    "not_active": "Inattivo"
  },
  "de": {
    "status": "Status",
    "status_info": "Aktiv, wenn in den letzten 5 Sekunden Daten übertragen wurden",
    "active": "Aktiv",
    "not_active": "Inaktiv"
  }
}
</i18n>
