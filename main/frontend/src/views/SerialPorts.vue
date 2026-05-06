<script setup lang="ts">
import { ref, computed, watch, onUnmounted } from 'vue';
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
const cacheSlaves = ref(0);
const cachePackets = ref(0);
const cacheLastPacketAgeUs = ref(0);
const cacheMapAgeUs = ref(0);
const cacheMemoryBytes = ref(0);
const cacheLoading = ref(false);

async function toggleCache(enabled: boolean) {
  cacheLoading.value = true;
  try {
    if (enabled) {
      // Enable: switch both ports to cache_bus mode
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'cache_bus' } });
      // Stats will be fetched by the watcher-driven fetchCacheStats() polling
    } else {
      // Disable: switch both ports back to tcp_bridge mode
      await api<void>('ports/1/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
      await api<void>('ports/2/mode', { method: 'POST', json: { mode: 'tcp_bridge' } });
    }
  } catch {
    // Ignore errors silently
  } finally {
    cacheLoading.value = false;
  }
}

function downloadCacheCsv() {
  const prefix = import.meta.env.DEV ? '/api/' : '/';
  window.open(`${prefix}cache/csv`);
}

async function fetchCacheStats(): Promise<void> {
  if (!cacheEnabled.value) return;
  try {
    const s = await api<{
      enabled: boolean;
      entries: number;
      slaves: number;
      packets_processed: number;
      last_packet_age_us: number;
      map_age_us: number;
      memory_bytes: number;
    }>('cache/status');
    cacheEntries.value           = s.entries;
    cacheSlaves.value            = s.slaves;
    cachePackets.value           = s.packets_processed;
    cacheLastPacketAgeUs.value   = s.last_packet_age_us;
    cacheMapAgeUs.value          = s.map_age_us;
    cacheMemoryBytes.value       = s.memory_bytes;
  } catch {
    // Silently ignore
  }
}

function formatAge(us: number): string {
  if (us === 0) return '—';
  const seconds = Math.floor(us / 1_000_000);
  if (seconds < 60) return `${seconds} s`;
  const minutes = Math.floor(seconds / 60);
  const secs = seconds % 60;
  if (minutes < 60) return `${minutes} min ${secs} s`;
  const hours = Math.floor(minutes / 60);
  const mins = minutes % 60;
  return `${hours} h ${mins} min`;
}

function formatMemory(bytes: number): string {
  if (bytes === 0) return '—';
  if (bytes < 1024) return `${bytes} B`;
  return `${(bytes / 1024).toFixed(1)} KB`;
}

let cacheStatsInterval: ReturnType<typeof setInterval> | null = null;

// { immediate: true } ensures stats are fetched on mount if cache is already enabled
// (watch fires immediately with the current value instead of waiting for first change).
// oldVal guard: only reset displayed stats when transitioning from a confirmed-enabled
// state (oldVal === true) to disabled — this prevents the stats panel from flashing to
// "—" on every info-polling reconnect where cacheEnabled briefly becomes false because
// info.value is temporarily undefined, even though the cache is still running on device.
watch(cacheEnabled, (val, oldVal) => {
  if (val) {
    fetchCacheStats(); // immediate fetch
    cacheStatsInterval = setInterval(fetchCacheStats, 5000);
  } else {
    if (cacheStatsInterval) { clearInterval(cacheStatsInterval); cacheStatsInterval = null; }
    // Only reset stats when transitioning from a known-enabled state, not from undefined/initial
    if (oldVal === true) {
      cacheEntries.value         = 0;
      cacheSlaves.value          = 0;
      cachePackets.value         = 0;
      cacheLastPacketAgeUs.value = 0;
      cacheMapAgeUs.value        = 0;
      cacheMemoryBytes.value     = 0;
    }
  }
}, { immediate: true });

onUnmounted(() => {
  if (cacheStatsInterval) { clearInterval(cacheStatsInterval); cacheStatsInterval = null; }
});
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div v-if="data" class="main-body">
      <div class="grid-2">
        <RsSettings
          v-model:settings="data.rs485_1"
          field="rs485_1"
          title="RS-485 · Port 1"
          sub="Wired terminal · left"
        />

        <div class="stack">
          <RsSettings
            v-model:settings="data.rs485_2"
            field="rs485_2"
            title="RS-485 · Port 2"
            sub="Wired terminal · right + I/O bus"
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
                  <div style="justify-self: end"><Switch id="io_bus" v-model="data.io_bus" /></div>
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
              <Switch id="cache_mm" :model-value="cacheEnabled" :disabled="cacheLoading" @update:model-value="(v) => toggleCache(v ?? false)" />
            </div>
            <div class="card-body">
              <div class="cache-stats">
                <div class="cache-stat">
                  <div class="cache-stat-label">{{ t('stat_slaves_regs') }}</div>
                  <div class="cache-stat-sub">{{ t('stat_seen_on_bus') }}</div>
                  <div class="cache-stat-value">
                    <span :class="cacheEnabled ? 'accent' : 'muted'">{{ cacheEnabled ? cacheSlaves : '—' }}</span>
                    <span class="muted"> / {{ cacheEnabled ? cacheEntries : '—' }}</span>
                  </div>
                </div>
                <div class="cache-stat">
                  <div class="cache-stat-label">{{ t('stat_packets') }}</div>
                  <div class="cache-stat-sub">{{ t('stat_since_reset') }}</div>
                  <div class="cache-stat-value">{{ cacheEnabled ? cachePackets : '—' }}</div>
                </div>
                <div class="cache-stat">
                  <div class="cache-stat-label">{{ t('stat_last_packet') }}</div>
                  <div class="cache-stat-sub">{{ t('stat_ago') }}</div>
                  <div class="cache-stat-value">{{ cacheEnabled ? formatAge(cacheLastPacketAgeUs) : '—' }}</div>
                </div>
                <div class="cache-stat">
                  <div class="cache-stat-label">{{ t('stat_map_age') }}</div>
                  <div class="cache-stat-sub">{{ t('stat_since_reset') }}</div>
                  <div class="cache-stat-value">{{ cacheEnabled ? formatAge(cacheMapAgeUs) : '—' }}</div>
                </div>
                <div class="cache-stat">
                  <div class="cache-stat-label">{{ t('stat_memory') }}</div>
                  <div class="cache-stat-sub">&nbsp;</div>
                  <div class="cache-stat-value">{{ cacheEnabled ? formatMemory(cacheMemoryBytes) : '—' }}</div>
                </div>
              </div>
              <div class="field" style="margin-top:12px">
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
.cache-stats {
  display: grid;
  grid-template-columns: repeat(5, 1fr);
  border: 1px solid var(--border-color);
  border-radius: var(--r-sm);
  margin-bottom: 12px;
}
.cache-stat {
  padding: 10px 14px;
  border-right: 1px solid var(--border-color);
}
.cache-stat:last-child {
  border-right: none;
}
.cache-stat-label {
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.04em;
  text-transform: uppercase;
  color: var(--text-secondary);
  white-space: nowrap;
}
.cache-stat-sub {
  font-size: 11px;
  color: var(--text-secondary);
  margin-bottom: 8px;
}
.cache-stat-value {
  font-size: 18px;
  font-weight: 500;
}
.accent {
  color: var(--primary-color, #2563eb);
  font-weight: 700;
}

</style>

<i18n>
{
  "en": { "title": "Serial ports", "crumbs": "RS-485 interfaces", "save": "Save", "io_bus_sub": "WB-MIO chip connected to RS-485 Port 2. Default address 247.", "io_bus_enable": "Enable I/O Bus", "cache_mm_title": "Caching Multimaster", "cache_mm_sub": "Passive register value cache for FC03/FC04 read exchanges", "cache_download": "Export", "cache_download_btn": "Download CSV", "stat_slaves_regs": "SLAVES / REGISTERS", "stat_seen_on_bus": "seen on bus", "stat_packets": "PACKETS PROCESSED", "stat_since_reset": "since last reset", "stat_last_packet": "LAST PACKET", "stat_ago": "ago", "stat_map_age": "MAP AGE", "stat_memory": "MEMORY" },
  "ru": { "title": "Последовательные порты", "crumbs": "Интерфейсы RS-485", "save": "Сохранить", "io_bus_sub": "Чип WB-MIO, подключённый к RS-485 Port 2. Адрес по умолчанию 247.", "io_bus_enable": "Включить I/O Bus", "cache_mm_title": "Кеширующий мультимастер", "cache_mm_sub": "Пассивный кэш значений регистров для запросов FC03/FC04", "cache_download": "Экспорт", "cache_download_btn": "Скачать CSV", "stat_slaves_regs": "УСТРОЙСТВА / РЕГИСТРЫ", "stat_seen_on_bus": "на шине", "stat_packets": "ПАКЕТОВ ОБРАБОТАНО", "stat_since_reset": "с последнего сброса", "stat_last_packet": "ПОСЛЕДНИЙ ПАКЕТ", "stat_ago": "назад", "stat_map_age": "ВОЗРАСТ КАРТЫ", "stat_memory": "ПАМЯТЬ" },
  "kk": { "title": "Сериялық порттар", "crumbs": "RS-485 интерфейстері", "save": "Сақтау", "io_bus_sub": "RS-485 Port 2-ге қосылған WB-MIO чипі. Әдепкі адресі 247.", "io_bus_enable": "I/O Bus қосу", "cache_mm_title": "Кэштеуші мультимастер", "cache_mm_sub": "FC03/FC04 оқу алмасулары үшін регистр мәндерінің пассивті кэші", "cache_download": "Экспорт", "cache_download_btn": "CSV жүктеу", "stat_slaves_regs": "ҚҰРЫЛҒЫЛАР / РЕГИСТРЛЕР", "stat_seen_on_bus": "шинада", "stat_packets": "ӨҢДЕЛГЕН ПАКЕТТЕР", "stat_since_reset": "соңғы қалпына келтіруден бері", "stat_last_packet": "СОҢҒЫ ПАКЕТ", "stat_ago": "бұрын", "stat_map_age": "КАРТА ЖАСЫ", "stat_memory": "ЖАД" },
  "it": { "title": "Porte seriali", "crumbs": "Interfacce RS-485", "save": "Salva", "io_bus_sub": "Chip WB-MIO collegato alla RS-485 Port 2. Indirizzo predefinito 247.", "io_bus_enable": "Abilita I/O Bus", "cache_mm_title": "Cache Multimaster", "cache_mm_sub": "Cache passivo dei valori di registro per scambi FC03/FC04", "cache_download": "Esporta", "cache_download_btn": "Scarica CSV", "stat_slaves_regs": "SLAVE / REGISTRI", "stat_seen_on_bus": "visti sul bus", "stat_packets": "PACCHETTI ELABORATI", "stat_since_reset": "dall'ultimo reset", "stat_last_packet": "ULTIMO PACCHETTO", "stat_ago": "fa", "stat_map_age": "ETÀ DELLA MAPPA", "stat_memory": "MEMORIA" },
  "de": { "title": "Serielle Schnittstellen", "crumbs": "RS-485-Schnittstellen", "save": "Speichern", "io_bus_sub": "WB-MIO-Chip an RS-485 Port 2 angeschlossen. Standardadresse 247.", "io_bus_enable": "I/O Bus aktivieren", "cache_mm_title": "Caching Multimaster", "cache_mm_sub": "Passiver Registerwert-Cache für FC03/FC04-Lesevorgänge", "cache_download": "Export", "cache_download_btn": "CSV herunterladen", "stat_slaves_regs": "SLAVES / REGISTER", "stat_seen_on_bus": "am Bus gesehen", "stat_packets": "VERARBEITETE PAKETE", "stat_since_reset": "seit letztem Reset", "stat_last_packet": "LETZTES PAKET", "stat_ago": "vor", "stat_map_age": "KARTENALTER", "stat_memory": "SPEICHER" }
}
</i18n>
