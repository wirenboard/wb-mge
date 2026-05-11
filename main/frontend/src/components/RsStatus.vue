<script setup lang="ts">
import { useI18n } from 'vue-i18n';
import type { RsStatus } from '@/common/types';
import Info from '@/components/Info.vue';

defineProps<{ title: string; info: RsStatus }>();

const { t } = useI18n();
</script>

<template>
  <div class="rsStatus-label"><b>{{ title }}</b></div>
  <div></div>

  <div class="rsStatus-label">{{ t('status') }}</div>
  <div class="rsStatus-value">{{ info.is_busy ? t('active') : t('not_active') }}</div>
  <Info class="rsStatus-info" :text="t('status_info')" />
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
