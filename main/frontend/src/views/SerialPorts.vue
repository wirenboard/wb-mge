<script setup lang="ts">
import { ref, computed } from 'vue';
import { useI18n } from 'vue-i18n';
import { useSettings } from '@/common/settings';
import { useInfo } from '@/common/info';
import { api } from '@/utils/api';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import Layout from '@/components/Layout.vue';
import RsSettings from '@/components/RsSettings.vue';
import Switch from '@/components/Switch.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
const { info } = useInfo();

// Cache is considered enabled when ALL ports are in cache_bus mode.
// Derived reactively from the info ref polled globally every 5 s by App.vue.
const cacheEnabled = computed(() => {
  if (!info.value) return false;
  return info.value.rs485_1.port_mode === 'cache_bus' &&
         info.value.rs485_2.port_mode === 'cache_bus';
});

const cacheEntries = ref(0);
const cacheMaxEntries = ref(1024); // Default fallback; overwritten from cache/status response
const cachePort = ref<'1' | '2'>('1');
const cacheLoading = ref(false);

async function toggleCache(enabled: boolean) {
  cacheLoading.value = true;
  try {
    if (enabled) {
      // Enable: switch both ports to cache_bus mode
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      // Fetch entries count and capacity now that cache is active
      const cacheStatus = await api<{ enabled: boolean; entries: number; max_entries: number }>('cache/status');
      cacheEntries.value = cacheStatus.entries;
      cacheMaxEntries.value = cacheStatus.max_entries ?? 1024;
    } else {
      // Disable: switch both ports back to tcp_bridge mode
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
      cacheEntries.value = 0;
    }
  } catch {
    // Ignore errors silently
  } finally {
    cacheLoading.value = false;
  }
}

function setCachePort(p: string) {
  if (p === '1' || p === '2') cachePort.value = p;
}

function downloadCacheCsv() {
  const prefix = import.meta.env.DEV ? '/api/' : '/';
  window.open(`${prefix}cache/csv?port=${cachePort.value}`);
}
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <RsSettings
          v-model:settings="data.rs485_1"
          field="rs485_1"
          :title="t('port_1')"
          :sub="t('port1_sub')"
        />

        <div class="stack">
          <RsSettings
            v-model:settings="data.rs485_2"
            field="rs485_2"
            :title="t('port_2')"
            :sub="t('port2_sub')"
          />

          <section class="card">
            <form @submit.prevent="updateSettings({ io_bus: data.io_bus })">
              <div class="card-header">
                <div class="card-title-wrap">
                  <div class="title">I/O Bus</div>
                  <div class="sub">{{ t('io_bus_sub') }}</div>
                </div>
                <Button
                  type="submit"
                  :is-loading="isLoading && isChanged(['io_bus'])"
                  :disabled="isLoading || !isChanged(['io_bus'])"
                >
                  {{ t('save') }}
                </Button>
              </div>
              <div class="card-body">
                <div class="field">
                  <label for="io_bus">{{ t('io_bus_enable') }}</label>
                  <div class="switch-end"><Switch id="io_bus" v-model="data.io_bus" /></div>
                </div>
              </div>
            </form>
          </section>

          <section class="card">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">{{ t('cache_mm_title') }}</div>
                <div class="sub">{{ t('cache_mm_sub') }}</div>
              </div>
              <div style="display:flex;align-items:center;gap:10px">
                <span v-if="cacheEnabled" class="muted" style="font-size:12px">{{ cacheEntries }} / {{ cacheMaxEntries }} {{ t('cache_entries') }}</span>
                <Switch id="cache_mm" :model-value="cacheEnabled" :disabled="cacheLoading" @update:model-value="(v) => toggleCache(v ?? false)" />
              </div>
            </div>
            <div class="card-body">
              <div class="field">
                <label>{{ t('cache_port') }}</label>
                <div class="port-btns">
                  <button
                    v-for="p in ['1', '2']"
                    :key="p"
                    type="button"
                    :class="['port-btn', { active: cachePort === p }]"
                    @click="setCachePort(p)"
                  >Port {{ p }}</button>
                </div>
              </div>
              <div class="field">
                <label>{{ t('cache_download') }}</label>
                <Button
                  type="button"
                  variant="outline"
                  :disabled="!cacheEnabled || cacheEntries === 0"
                  @click="downloadCacheCsv"
                >{{ t('cache_download_btn') }}</Button>
              </div>
            </div>
          </section>
        </div>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
.port-btns {
  display: flex;
  gap: 4px;
}

.port-btn {
  height: 32px;
  padding: 0 12px;
  font-size: 13px;
  background: transparent;
  border: 1px solid var(--border-color);
  color: var(--text-secondary);
  border-radius: var(--r-sm);
  cursor: pointer;
  transition: background 0.12s, border-color 0.12s, color 0.12s;
}

.port-btn:hover {
  background: var(--bg-surface-subtle);
  border-color: var(--border-strong);
}

.port-btn.active {
  background: var(--primary-color);
  border-color: var(--primary-color);
  color: #fff;
}
</style>

<i18n>
{
  "en": {
    "title": "Serial ports",
    "crumbs": "RS-485 interfaces",
    "save": "Save",
    "io_bus_sub": "WB-MIO chip connected to RS-485 Port 2. Default address 247.",
    "io_bus_enable": "Enable I/O Bus",
    "port1_sub": "Wired terminal · left",
    "port2_sub": "Wired terminal · right + I/O bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "cache_mm_title": "Caching Multimaster",
    "cache_mm_sub": "Passive register value cache for FC03/FC04 read exchanges",
    "cache_entries": "entries",
    "cache_port": "Port",
    "cache_download": "Export",
    "cache_download_btn": "Download CSV"
  },
  "ru": {
    "title": "Последовательные порты",
    "crumbs": "Интерфейсы RS-485",
    "save": "Сохранить",
    "io_bus_sub": "Чип WB-MIO, подключённый ко второму порту RS-485. Адрес по умолчанию 247.",
    "io_bus_enable": "Включить I/O Bus",
    "port1_sub": "Левый клеммник",
    "port2_sub": "Правый клеммник + I/O bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "cache_mm_title": "Кеширующий мультимастер",
    "cache_mm_sub": "Пассивный кэш значений регистров для запросов FC03/FC04",
    "cache_entries": "записей",
    "cache_port": "Порт",
    "cache_download": "Экспорт",
    "cache_download_btn": "Скачать CSV"
  },
  "kk": {
    "title": "Сериялық порттар",
    "crumbs": "RS-485 интерфейстері",
    "save": "Сақтау",
    "io_bus_sub": "RS-485 Порт 2-ге қосылған WB-MIO чипі. Әдепкі адресі 247.",
    "io_bus_enable": "I/O Bus қосу",
    "port1_sub": "Сымды клемма · сол",
    "port2_sub": "Сымды клемма · оң + I/O bus",
    "port_1": "RS-485 · Порт 1",
    "port_2": "RS-485 · Порт 2",
    "cache_mm_title": "Кэштеуші мультимастер",
    "cache_mm_sub": "FC03/FC04 оқу алмасулары үшін регистр мәндерінің пассивті кэші",
    "cache_entries": "жазба",
    "cache_port": "Порт",
    "cache_download": "Экспорт",
    "cache_download_btn": "CSV жүктеу"
  },
  "it": {
    "title": "Porte seriali",
    "crumbs": "Interfacce RS-485",
    "save": "Salva",
    "io_bus_sub": "Chip WB-MIO collegato alla RS-485 Port 2. Indirizzo predefinito 247.",
    "io_bus_enable": "Abilita I/O Bus",
    "port1_sub": "Morsettiera · sinistra",
    "port2_sub": "Morsettiera · destra + I/O bus",
    "port_1": "RS-485 · Porta 1",
    "port_2": "RS-485 · Porta 2",
    "cache_mm_title": "Cache Multimaster",
    "cache_mm_sub": "Cache passivo dei valori di registro per scambi FC03/FC04",
    "cache_entries": "voci",
    "cache_port": "Porta",
    "cache_download": "Esporta",
    "cache_download_btn": "Scarica CSV"
  },
  "de": {
    "title": "Serielle Schnittstellen",
    "crumbs": "RS-485-Schnittstellen",
    "save": "Speichern",
    "io_bus_sub": "WB-MIO-Chip an RS-485 Port 2 angeschlossen. Standardadresse 247.",
    "io_bus_enable": "I/O Bus aktivieren",
    "port1_sub": "Klemmenleiste · links",
    "port2_sub": "Klemmenleiste · rechts + I/O bus",
    "port_1": "RS-485 · Port 1",
    "port_2": "RS-485 · Port 2",
    "cache_mm_title": "Caching Multimaster",
    "cache_mm_sub": "Passiver Registerwert-Cache für FC03/FC04-Lesevorgänge",
    "cache_entries": "Einträge",
    "cache_port": "Port",
    "cache_download": "Export",
    "cache_download_btn": "CSV herunterladen"
  }
}
</i18n>
