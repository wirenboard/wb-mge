<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, shallowRef, watch } from 'vue';
import { useI18n } from 'vue-i18n';
import { deviceClockOf, useGps } from '@/common/gps';
import {
  SNR_HIGH_DBHZ,
  SNR_LOW_DBHZ,
  type SkyDetail,
  type SkySatellite,
  type SnrBand,
  snrBand,
} from '@/common/skyPlot';
import type { GpsSatellite, GpsSatelliteSystem } from '@/common/types';
import GpsClock from '@/components/GpsClock.vue';
import GpsSkyPlot from '@/components/GpsSkyPlot.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InfoRow from '@/components/InfoRow.vue';
import Layout from '@/components/Layout.vue';

const DASH = '—';
const MS_PER_S = 1000;
const US_PER_S = 1e6;
const PPM = 1e6;
const S_PER_MIN = 60;
const S_PER_H = 3600;
const S_PER_D = 86400;
const DURATION_UNITS_SHOWN = 2;
// Below this an age reads best in milliseconds, above it as a duration.
const AGE_AS_MS_BELOW = 10000;
const COORD_DIGITS = 6;
const OFFSET_DIGITS = 1;
const PPM_DIGITS = 2;
const AVG_INTERVAL_DIGITS = 2;
const DOP_DIGITS = 2;
const MOTION_DIGITS = 1;
const SATELLITE_SYSTEM_ORDER: GpsSatelliteSystem[] = ['GPS', 'GLONASS', 'Galileo', 'BeiDou', 'QZSS', 'unknown'];
// A satellite missing from a report keeps its place, with its last values, for this long: weak ones
// drop in and out of the receiver's list every other second, and each drop would add or remove a marker.
const SATELLITE_HOLD_MS = 10000;
const SNR_SWATCH_CLASSES: Record<SnrBand, string> = {
  strong: 'satSky-swatchStrong',
  medium: 'satSky-swatchMedium',
  weak: 'satSky-swatchWeak',
  none: 'satSky-swatchNone',
};

const RMC_STATUS_KEYS: Record<string, string> = { A: 'rmc_status_valid', V: 'rmc_status_invalid' };
const FIX_MODE_KEYS: Record<string, string> = { 1: 'fix_mode_none', 2: 'fix_mode_2d', 3: 'fix_mode_3d' };
const ANTENNA_KEYS: Record<string, string> = {
  ok: 'antenna_ok',
  open: 'antenna_open',
  short: 'antenna_short',
  unknown: 'antenna_unknown',
};

const { t, locale } = useI18n();
const { sample, failed, startPolling, stopPolling } = useGps();

onMounted(startPolling);
onBeforeUnmount(stopPolling);

const status = computed(() => sample.value?.status);
const clock = computed(() => (sample.value ? deviceClockOf(sample.value) : null));

const isKnown = (value: number | null | undefined): value is number =>
  typeof value === 'number' && Number.isFinite(value);

const formatNumber = (value: number, fractionDigits = 0, signed = false) =>
  new Intl.NumberFormat(locale.value, {
    minimumFractionDigits: fractionDigits,
    maximumFractionDigits: fractionDigits,
    signDisplay: signed ? 'exceptZero' : 'auto',
  }).format(value);

const decimal = (value: number | null | undefined, fractionDigits: number) =>
  (isKnown(value) ? formatNumber(value, fractionDigits) : DASH);

const count = (value: number | null | undefined) => decimal(value, 0);

const withUnit = (key: string, value: number | null | undefined, fractionDigits = 0, signed = false) =>
  isKnown(value) ? t(key, { v: formatNumber(value, fractionDigits, signed) }) : DASH;

// Firmware enums through a key table: a value this page does not know yet is shown as sent.
const enumLabel = (keys: Record<string, string>, value: string | number | null | undefined) => {
  if (value === null || value === undefined) {
    return DASH;
  }
  const key = keys[String(value)];
  return key ? t(key) : String(value);
};

// The two most significant units: "2 d 3 h", "4 min 12 s", "7 s".
const formatDuration = (totalSeconds: number) => {
  const seconds = Math.floor(totalSeconds);
  const parts = [
    { key: 'dur_d', n: Math.floor(seconds / S_PER_D) },
    { key: 'dur_h', n: Math.floor((seconds % S_PER_D) / S_PER_H) },
    { key: 'dur_min', n: Math.floor((seconds % S_PER_H) / S_PER_MIN) },
    { key: 'dur_s', n: seconds % S_PER_MIN },
  ];
  const first = parts.findIndex((part) => part.n > 0);
  const start = first < 0 ? parts.length - 1 : first;
  return parts
    .slice(start, start + DURATION_UNITS_SHOWN)
    .map((part) => t(part.key, { n: part.n }))
    .join(' ');
};

const duration = (seconds: number | null | undefined) => (isKnown(seconds) ? formatDuration(seconds) : DASH);

const age = (ms: number | null | undefined) => {
  if (!isKnown(ms)) {
    return DASH;
  }
  return ms < AGE_AS_MS_BELOW ? t('value_ms', { v: formatNumber(ms) }) : formatDuration(ms / MS_PER_S);
};

const offset = computed(() => {
  const value = clock.value;
  if (!value) {
    return DASH;
  }
  return t('value_offset', {
    v: formatNumber(value.offsetMs, OFFSET_DIGITS, true),
    err: formatNumber(value.uncertaintyMs, OFFSET_DIGITS),
  });
});

// The device counts microseconds on its own oscillator, so an average PPS interval above 1 s means
// that oscillator runs fast.
const crystalErrorPpm = computed(() => {
  const avg = status.value?.pps.interval_avg_us;
  return isKnown(avg) ? ((avg - US_PER_S) / US_PER_S) * PPM : null;
});

const rankSystem = (system: GpsSatelliteSystem) => {
  const index = SATELLITE_SYSTEM_ORDER.indexOf(system);
  return index < 0 ? SATELLITE_SYSTEM_ORDER.length : index;
};

const satelliteKey = (sat: GpsSatellite) => `${sat.system}-${sat.prn}`;

const satelliteLastSeen = new Map<string, { sat: GpsSatellite; seenMs: number }>();
const satellites = shallowRef<GpsSatellite[]>([]);

watch(status, (current) => {
  if (!current) {
    return;
  }
  const now = performance.now();
  for (const sat of current.satellites) {
    satelliteLastSeen.set(satelliteKey(sat), { sat, seenMs: now });
  }
  for (const [key, entry] of satelliteLastSeen) {
    if ((now - entry.seenMs) > SATELLITE_HOLD_MS) {
      satelliteLastSeen.delete(key);
    }
  }
  satellites.value = [...satelliteLastSeen.values()]
    .map((entry) => entry.sat)
    .sort((a, b) => (rankSystem(a.system) - rankSystem(b.system)) || (a.prn - b.prn));
});

const systemLabel = (system: GpsSatelliteSystem) => (system === 'unknown' ? t('sat_system_unknown') : system);

const satelliteDetails = (sat: GpsSatellite): SkyDetail[] => [
  { label: t('sat_system'), value: systemLabel(sat.system) },
  { label: t('sat_prn'), value: String(sat.prn) },
  { label: t('sat_elevation'), value: withUnit('value_deg', sat.elevation) },
  { label: t('sat_azimuth'), value: withUnit('value_deg', sat.azimuth) },
  { label: t('sat_snr'), value: withUnit('value_dbhz', sat.snr) },
  { label: t('sat_used'), value: sat.used ? t('yes') : t('no') },
];

type PlacedSatellite = GpsSatellite & { elevation: number; azimuth: number };

const isPlaced = (sat: GpsSatellite): sat is PlacedSatellite => isKnown(sat.elevation) && isKnown(sat.azimuth);

const skySatellites = computed<SkySatellite[]>(() => satellites.value.filter(isPlaced).map((sat) => ({
  key: satelliteKey(sat),
  prn: sat.prn,
  elevation: sat.elevation,
  azimuth: sat.azimuth,
  snr: sat.snr,
  used: sat.used,
  details: satelliteDetails(sat),
})));

// GSV leaves elevation and azimuth empty until the receiver knows the satellite's orbit (no almanac
// yet), so there is nowhere on the plot to put it.
const unplacedSatellites = computed(() => satellites.value.filter((sat) => !isPlaced(sat)));

const degreeLabel = (value: number) => t('value_deg', { v: formatNumber(value) });
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <Info v-if="failed" severity="error" :text="status ? t('request_failed') : t('request_failed_no_data')" />

      <template v-if="status">
        <div class="grid-2">
          <div class="stack">
            <section class="card">
              <div class="card-header">
                <div class="title">{{ t('time') }}</div>
                <span v-if="status.time.synced" class="pill ok"><span class="dot" />{{ t('synced') }}</span>
                <span v-else class="pill muted">{{ t('not_synced') }}</span>
              </div>
              <div class="card-body">
                <GpsClock :unix-ms="clock?.unixMs ?? null" :perf-ms="clock?.perfMs ?? 0" :caption="t('utc_time')" />
                <InfoRow :label="t('clock_offset')">
                  <span class="mono">{{ offset }}</span>
                  <template #hint>{{ t('clock_offset_hint') }}</template>
                </InfoRow>
                <InfoRow :label="t('locked_for')"><span class="mono">{{ duration(status.time.locked_for_s) }}</span></InfoRow>
                <InfoRow :label="t('lock_count')"><span class="mono">{{ count(status.time.lock_count) }}</span></InfoRow>
                <InfoRow :label="t('loss_count')"><span class="mono">{{ count(status.time.loss_count) }}</span></InfoRow>
                <InfoRow :label="t('last_pps_age')"><span class="mono">{{ age(status.time.last_pps_age_ms) }}</span></InfoRow>
                <InfoRow :label="t('anchor_age')"><span class="mono">{{ age(status.time.anchor_age_ms) }}</span></InfoRow>
              </div>
            </section>

            <section class="card">
              <div class="card-header">
                <div class="card-title-wrap">
                  <div class="title">{{ t('pps') }}</div>
                  <div class="sub">{{ t('pps_sub') }}</div>
                </div>
              </div>
              <div class="card-body">
                <InfoRow :label="t('pps_count')"><span class="mono">{{ count(status.pps.count) }}</span></InfoRow>
                <InfoRow :label="t('pps_interval')"><span class="mono">{{ withUnit('value_us', status.pps.interval_us) }}</span></InfoRow>
                <InfoRow :label="t('pps_interval_min')"><span class="mono">{{ withUnit('value_us', status.pps.interval_min_us) }}</span></InfoRow>
                <InfoRow :label="t('pps_interval_max')"><span class="mono">{{ withUnit('value_us', status.pps.interval_max_us) }}</span></InfoRow>
                <InfoRow :label="t('pps_interval_avg')">
                  <span class="mono">{{ withUnit('value_us', status.pps.interval_avg_us, AVG_INTERVAL_DIGITS) }}</span>
                </InfoRow>
                <InfoRow :label="t('crystal_error')">
                  <span class="mono">{{ withUnit('value_ppm', crystalErrorPpm, PPM_DIGITS, true) }}</span>
                  <template #hint>{{ t('crystal_error_hint') }}</template>
                </InfoRow>
                <InfoRow :label="t('rmc_delay')"><span class="mono">{{ withUnit('value_ms', status.pps.rmc_delay_ms) }}</span></InfoRow>
              </div>
            </section>
          </div>

          <div class="stack">
            <section class="card">
              <div class="card-header">
                <div class="title">{{ t('fix') }}</div>
              </div>
              <div class="card-body">
                <InfoRow :label="t('rmc_status')">{{ enumLabel(RMC_STATUS_KEYS, status.fix.rmc_status) }}</InfoRow>
                <InfoRow :label="t('fix_mode')">{{ enumLabel(FIX_MODE_KEYS, status.fix.mode) }}</InfoRow>
                <InfoRow :label="t('fix_quality')"><span class="mono">{{ count(status.fix.quality) }}</span></InfoRow>
                <InfoRow :label="t('sats')">
                  <span class="mono">{{ count(status.fix.sats_used) }} / {{ count(status.fix.sats_in_view) }}</span>
                </InfoRow>
                <InfoRow :label="t('pdop')"><span class="mono">{{ decimal(status.fix.pdop, DOP_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('hdop')"><span class="mono">{{ decimal(status.fix.hdop, DOP_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('vdop')"><span class="mono">{{ decimal(status.fix.vdop, DOP_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('lat')"><span class="mono">{{ withUnit('value_deg', status.fix.lat, COORD_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('lon')"><span class="mono">{{ withUnit('value_deg', status.fix.lon, COORD_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('alt')"><span class="mono">{{ withUnit('value_m', status.fix.alt_m, MOTION_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('speed')"><span class="mono">{{ withUnit('value_kmh', status.fix.speed_kmh, MOTION_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('course')"><span class="mono">{{ withUnit('value_deg', status.fix.course_deg, MOTION_DIGITS) }}</span></InfoRow>
                <InfoRow :label="t('antenna')">
                  <span :class="{ 'gps-alarm': status.antenna === 'short' }">{{ enumLabel(ANTENNA_KEYS, status.antenna) }}</span>
                  <template v-if="status.antenna === 'open'" #hint>{{ t('antenna_passive_hint') }}</template>
                </InfoRow>
              </div>
            </section>
          </div>
        </div>

        <div class="grid-2">
          <section class="card">
            <div class="card-header">
              <div class="title">{{ t('nmea') }}</div>
              <span v-if="status.nmea.link === 'ok'" class="pill ok"><span class="dot" />{{ t('nmea_link_ok') }}</span>
              <span v-else class="pill muted">{{ t('nmea_link_detecting') }}</span>
            </div>
            <div class="card-body">
              <InfoRow :label="t('nmea_baud')"><span class="mono">{{ t('value_bps', { v: status.nmea.baud }) }}</span></InfoRow>
              <InfoRow :label="t('nmea_bytes')"><span class="mono">{{ count(status.nmea.bytes) }}</span></InfoRow>
              <InfoRow :label="t('nmea_sentences')"><span class="mono">{{ count(status.nmea.sentences) }}</span></InfoRow>
              <InfoRow :label="t('nmea_checksum_errors')"><span class="mono">{{ count(status.nmea.checksum_errors) }}</span></InfoRow>
              <InfoRow :label="t('nmea_rmc_valid')"><span class="mono">{{ count(status.nmea.rmc_valid) }}</span></InfoRow>
              <InfoRow :label="t('nmea_rmc_invalid')"><span class="mono">{{ count(status.nmea.rmc_invalid) }}</span></InfoRow>
            </div>
          </section>

          <section class="card">
            <div class="card-header">
              <div class="title">{{ t('ntp') }}</div>
            </div>
            <div class="card-body">
              <InfoRow :label="t('ntp_requests')"><span class="mono">{{ count(status.ntp.requests) }}</span></InfoRow>
              <InfoRow :label="t('ntp_answered_synced')"><span class="mono">{{ count(status.ntp.answered_synced) }}</span></InfoRow>
              <InfoRow :label="t('ntp_answered_unsynced')"><span class="mono">{{ count(status.ntp.answered_unsynced) }}</span></InfoRow>
              <InfoRow :label="t('ntp_ignored')"><span class="mono">{{ count(status.ntp.ignored) }}</span></InfoRow>
              <InfoRow :label="t('ntp_last_client')"><span class="mono">{{ status.ntp.last_client ?? DASH }}</span></InfoRow>
              <InfoRow :label="t('ntp_last_request_age')"><span class="mono">{{ age(status.ntp.last_request_age_ms) }}</span></InfoRow>
            </div>
          </section>
        </div>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('satellites') }}</div>
          </div>
          <div class="card-body satSky">
            <div class="satSky-overview">
              <div class="satSky-plot">
                <GpsSkyPlot :satellites="skySatellites" :degree-label="degreeLabel" />
              </div>
              <div class="satSky-legend">
                <ul class="satSky-group">
                  <li class="satSky-entry">
                    <span class="satSky-swatch satSky-swatchStrong" />
                    {{ t('legend_strong', { v: SNR_HIGH_DBHZ }) }}
                  </li>
                  <li class="satSky-entry">
                    <span class="satSky-swatch satSky-swatchMedium" />
                    {{ t('legend_medium', { low: SNR_LOW_DBHZ, high: SNR_HIGH_DBHZ }) }}
                  </li>
                  <li class="satSky-entry">
                    <span class="satSky-swatch satSky-swatchWeak" />
                    {{ t('legend_weak', { v: SNR_LOW_DBHZ }) }}
                  </li>
                  <li class="satSky-entry">
                    <span class="satSky-swatch satSky-swatchNone" />
                    {{ t('legend_no_signal') }}
                  </li>
                </ul>
                <ul class="satSky-group">
                  <li class="satSky-entry">
                    <span class="satSky-swatch satSky-swatchUsed" />
                    {{ t('legend_used') }}
                  </li>
                  <li class="satSky-entry">
                    <span class="satSky-swatch satSky-swatchUnused" />
                    {{ t('legend_unused') }}
                  </li>
                </ul>
              </div>
            </div>

            <p v-if="!satellites.length" class="satSky-note">{{ t('no_satellites') }}</p>
            <div v-else-if="unplacedSatellites.length" class="satSky-unplaced">
              <p class="satSky-note">{{ t('unplaced_caption') }}</p>
              <ul class="satSky-chips">
                <li v-for="sat in unplacedSatellites" :key="satelliteKey(sat)" class="satChip">
                  <span class="satSky-swatch satChip-dot" :class="SNR_SWATCH_CLASSES[snrBand(sat.snr)]" />
                  <span>{{ systemLabel(sat.system) }}</span>
                  <span class="mono">{{ sat.prn }}</span>
                  <span class="satChip-snr mono">{{ withUnit('value_dbhz', sat.snr) }}</span>
                  <dl class="satChip-popup">
                    <template v-for="row in satelliteDetails(sat)" :key="row.label">
                      <dt class="satChip-label">{{ row.label }}</dt>
                      <dd class="satChip-value">{{ row.value }}</dd>
                    </template>
                  </dl>
                </li>
              </ul>
            </div>
          </div>
        </section>
      </template>
    </div>
  </Layout>
</template>

<style scoped>
.gps-alarm {
  color: var(--danger-color);
}

.satSky-overview {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 16px 40px;
  padding-top: 8px;
}

/* The plot fills the card up to this width, then stops growing and the legend takes the rest. */
.satSky-plot {
  flex: 0 1 400px;
  min-width: 0;
}

.satSky-legend {
  flex: 1 1 220px;
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.satSky-group {
  list-style: none;
  margin: 0;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.satSky-entry {
  display: flex;
  align-items: center;
  gap: 10px;
  color: var(--text-secondary);
}

.satSky-swatch {
  flex: none;
  width: 12px;
  height: 12px;
  border-radius: 50%;
}

.satSky-swatchStrong {
  background: var(--snr-strong);
}

.satSky-swatchMedium {
  background: var(--snr-medium);
}

.satSky-swatchWeak {
  background: var(--snr-weak);
}

.satSky-swatchNone {
  background: var(--snr-none);
}

/* The two marker shapes of the plot, in a neutral colour: solid when used, a tinted ring when not. */
.satSky-swatchUsed {
  background: var(--text-muted);
}

.satSky-swatchUnused {
  border: 2px solid var(--text-muted);
  background: color-mix(in oklch, var(--text-muted) 20%, var(--bg-surface));
}

.satSky-note {
  margin: 12px 0 0;
  color: var(--text-muted);
}

.satSky-unplaced {
  margin-top: 12px;
  border-top: 1px dashed var(--border-color);
}

.satSky-chips {
  list-style: none;
  margin: 10px 0 0;
  padding: 0;
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}

.satChip {
  position: relative;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 2px 10px 2px 8px;
  border: 1px solid var(--border-color);
  border-radius: 999px;
  background: var(--bg-surface-subtle);
  color: var(--text-secondary);
  font-size: 12px;
  line-height: 1.5;
  cursor: default;
}

.satChip:hover {
  border-color: var(--border-strong);
  color: var(--text-color);
}

.satChip-dot {
  width: 8px;
  height: 8px;
}

.satChip-snr {
  padding-left: 6px;
  border-left: 1px solid var(--border-color);
  color: var(--text-muted);
}

.satChip-popup {
  display: none;
  position: absolute;
  bottom: calc(100% + 6px);
  left: 50%;
  transform: translateX(-50%);
  z-index: 1;
  grid-template-columns: auto auto;
  gap: 2px 14px;
  margin: 0;
  padding: 8px 10px;
  background: var(--bg-surface);
  border: 1px solid var(--border-strong);
  border-radius: var(--r-md);
  box-shadow: var(--shadow-md);
  font-size: 11px;
  white-space: nowrap;
}

.satChip:hover .satChip-popup {
  display: grid;
}

.satChip-label {
  color: var(--text-muted);
}

.satChip-value {
  margin: 0;
  text-align: right;
  font-weight: 600;
  color: var(--text-color);
}
</style>

<i18n>
{
  "en": {
    "title": "GPS / NTP",
    "crumbs": "Time source, satellites and NTP server",
    "request_failed": "The device does not answer: the values below may be out of date",
    "request_failed_no_data": "Could not get GPS data from the device",

    "time": "Time",
    "utc_time": "Device time, UTC",
    "synced": "Synced",
    "not_synced": "Not synced",
    "clock_offset": "This computer's clock offset",
    "clock_offset_hint": "As in NTP: how much to add to this computer's clock to match the device",
    "locked_for": "Synced for",
    "lock_count": "Times synced",
    "loss_count": "Times sync lost",
    "last_pps_age": "Since last PPS pulse",
    "anchor_age": "Since last anchor (PPS + RMC)",

    "pps": "PPS",
    "pps_sub": "Pulse per second from the GPS receiver",
    "pps_count": "Pulses received",
    "pps_interval": "Last interval",
    "pps_interval_min": "Shortest interval",
    "pps_interval_max": "Longest interval",
    "pps_interval_avg": "Average interval",
    "crystal_error": "Crystal error (from average interval)",
    "crystal_error_hint": "Positive: the device's oscillator runs fast",
    "rmc_delay": "PPS → RMC delay",

    "fix": "Position fix",
    "rmc_status": "RMC status",
    "rmc_status_valid": "A (valid)",
    "rmc_status_invalid": "V (invalid)",
    "fix_mode": "Fix mode",
    "fix_mode_none": "No fix",
    "fix_mode_2d": "2D",
    "fix_mode_3d": "3D",
    "fix_quality": "Fix quality (GGA)",
    "pdop": "PDOP",
    "hdop": "HDOP",
    "vdop": "VDOP",
    "sats": "Satellites used / in view",
    "lat": "Latitude",
    "lon": "Longitude",
    "alt": "Altitude",
    "speed": "Speed",
    "course": "Course",
    "antenna": "Antenna",
    "antenna_ok": "OK",
    "antenna_open": "Open circuit",
    "antenna_short": "Short circuit",
    "antenna_unknown": "Unknown",
    "antenna_passive_hint": "A passive antenna draws no current, so it always reports “open”",

    "satellites": "Satellites",
    "sat_system": "System",
    "sat_prn": "PRN",
    "sat_elevation": "Elevation",
    "sat_azimuth": "Azimuth",
    "sat_snr": "SNR",
    "sat_used": "In fix",
    "sat_system_unknown": "Unknown",
    "no_satellites": "No satellites in view",
    "yes": "Yes",
    "no": "No",
    "legend_strong": "Strong signal: {v} dB-Hz and above",
    "legend_medium": "Medium signal: {low}–{high} dB-Hz",
    "legend_weak": "Weak signal: below {v} dB-Hz",
    "legend_no_signal": "No signal",
    "legend_used": "Used in the fix",
    "legend_unused": "Not used in the fix",
    "unplaced_caption": "Not on the map yet: the receiver hears these satellites but does not know their elevation and azimuth",

    "nmea": "NMEA link",
    "nmea_link_ok": "Receiving",
    "nmea_link_detecting": "Detecting baud rate…",
    "nmea_baud": "Baud rate",
    "nmea_bytes": "Bytes received",
    "nmea_sentences": "Sentences",
    "nmea_checksum_errors": "Checksum errors",
    "nmea_rmc_valid": "RMC valid",
    "nmea_rmc_invalid": "RMC invalid",

    "ntp": "NTP server",
    "ntp_requests": "Requests",
    "ntp_answered_synced": "Answered while synced",
    "ntp_answered_unsynced": "Answered while not synced",
    "ntp_ignored": "Ignored",
    "ntp_last_client": "Last client",
    "ntp_last_request_age": "Since last request",

    "value_ms": "{v} ms",
    "value_us": "{v} µs",
    "value_ppm": "{v} ppm",
    "value_m": "{v} m",
    "value_kmh": "{v} km/h",
    "value_deg": "{v}°",
    "value_dbhz": "{v} dB-Hz",
    "value_bps": "{v} bit/s",
    "value_offset": "{v} ms ± {err} ms",
    "dur_d": "{n} d",
    "dur_h": "{n} h",
    "dur_min": "{n} min",
    "dur_s": "{n} s"
  },
  "ru": {
    "title": "GPS / NTP",
    "crumbs": "Источник времени, спутники и NTP-сервер",
    "request_failed": "Устройство не отвечает: значения ниже могут быть устаревшими",
    "request_failed_no_data": "Не удалось получить данные GPS от устройства",

    "time": "Время",
    "utc_time": "Время устройства, UTC",
    "synced": "Синхронизировано",
    "not_synced": "Нет синхронизации",
    "clock_offset": "Смещение часов этого компьютера",
    "clock_offset_hint": "Как в NTP: сколько прибавить к часам компьютера, чтобы они совпали с устройством",
    "locked_for": "В синхронизации",
    "lock_count": "Синхронизаций",
    "loss_count": "Потерь синхронизации",
    "last_pps_age": "С последнего импульса PPS",
    "anchor_age": "С последней привязки (PPS + RMC)",

    "pps": "PPS",
    "pps_sub": "Секундные импульсы от GPS-приёмника",
    "pps_count": "Принято импульсов",
    "pps_interval": "Последний интервал",
    "pps_interval_min": "Минимальный интервал",
    "pps_interval_max": "Максимальный интервал",
    "pps_interval_avg": "Средний интервал",
    "crystal_error": "Уход кварца (по среднему интервалу)",
    "crystal_error_hint": "Положительный — генератор устройства спешит",
    "rmc_delay": "Задержка PPS → RMC",

    "fix": "Навигационное решение",
    "rmc_status": "Статус RMC",
    "rmc_status_valid": "A (достоверно)",
    "rmc_status_invalid": "V (недостоверно)",
    "fix_mode": "Тип решения",
    "fix_mode_none": "Нет решения",
    "fix_mode_2d": "2D",
    "fix_mode_3d": "3D",
    "fix_quality": "Качество решения (GGA)",
    "pdop": "PDOP",
    "hdop": "HDOP",
    "vdop": "VDOP",
    "sats": "Спутников в решении / видно",
    "lat": "Широта",
    "lon": "Долгота",
    "alt": "Высота",
    "speed": "Скорость",
    "course": "Курс",
    "antenna": "Антенна",
    "antenna_ok": "В норме",
    "antenna_open": "Обрыв",
    "antenna_short": "Короткое замыкание",
    "antenna_unknown": "Неизвестно",
    "antenna_passive_hint": "Пассивная антенна не потребляет ток, поэтому всегда определяется как «обрыв»",

    "satellites": "Спутники",
    "sat_system": "Система",
    "sat_prn": "PRN",
    "sat_elevation": "Угол места",
    "sat_azimuth": "Азимут",
    "sat_snr": "С/Ш",
    "sat_used": "В решении",
    "sat_system_unknown": "Неизвестна",
    "no_satellites": "Спутники не видны",
    "yes": "Да",
    "no": "Нет",
    "legend_strong": "Сильный сигнал: {v} дБ·Гц и выше",
    "legend_medium": "Средний сигнал: {low}–{high} дБ·Гц",
    "legend_weak": "Слабый сигнал: ниже {v} дБ·Гц",
    "legend_no_signal": "Нет сигнала",
    "legend_used": "Используется в решении",
    "legend_unused": "Не используется в решении",
    "unplaced_caption": "Пока не на карте: приёмник слышит эти спутники, но не знает их угол места и азимут",

    "nmea": "Канал NMEA",
    "nmea_link_ok": "Приём идёт",
    "nmea_link_detecting": "Определение скорости…",
    "nmea_baud": "Скорость",
    "nmea_bytes": "Принято байт",
    "nmea_sentences": "Сообщений",
    "nmea_checksum_errors": "Ошибок контрольной суммы",
    "nmea_rmc_valid": "RMC достоверных",
    "nmea_rmc_invalid": "RMC недостоверных",

    "ntp": "NTP-сервер",
    "ntp_requests": "Запросов",
    "ntp_answered_synced": "Отвечено при синхронизации",
    "ntp_answered_unsynced": "Отвечено без синхронизации",
    "ntp_ignored": "Проигнорировано",
    "ntp_last_client": "Последний клиент",
    "ntp_last_request_age": "С последнего запроса",

    "value_ms": "{v} мс",
    "value_us": "{v} мкс",
    "value_ppm": "{v} ppm",
    "value_m": "{v} м",
    "value_kmh": "{v} км/ч",
    "value_deg": "{v}°",
    "value_dbhz": "{v} дБ·Гц",
    "value_bps": "{v} бит/с",
    "value_offset": "{v} мс ± {err} мс",
    "dur_d": "{n} д",
    "dur_h": "{n} ч",
    "dur_min": "{n} мин",
    "dur_s": "{n} с"
  },
  "kk": {
    "title": "GPS / NTP",
    "crumbs": "Уақыт көзі, спутниктер және NTP сервері",
    "request_failed": "Құрылғы жауап бермейді: төмендегі мәндер ескірген болуы мүмкін",
    "request_failed_no_data": "Құрылғыдан GPS деректерін алу мүмкін болмады",

    "time": "Уақыт",
    "utc_time": "Құрылғы уақыты, UTC",
    "synced": "Синхрондалған",
    "not_synced": "Синхрондалмаған",
    "clock_offset": "Осы компьютер сағатының ығысуы",
    "clock_offset_hint": "NTP-дегідей: құрылғымен сәйкес келу үшін компьютер сағатына қосылатын шама",
    "locked_for": "Синхрондау ұзақтығы",
    "lock_count": "Синхрондау саны",
    "loss_count": "Синхрондау жоғалту саны",
    "last_pps_age": "Соңғы PPS импульсынан бері",
    "anchor_age": "Соңғы байланыстырудан бері (PPS + RMC)",

    "pps": "PPS",
    "pps_sub": "GPS қабылдағыштың секундтық импульстары",
    "pps_count": "Қабылданған импульстар",
    "pps_interval": "Соңғы интервал",
    "pps_interval_min": "Ең қысқа интервал",
    "pps_interval_max": "Ең ұзын интервал",
    "pps_interval_avg": "Орташа интервал",
    "crystal_error": "Кварц қателігі (орташа интервал бойынша)",
    "crystal_error_hint": "Оң мән — құрылғы генераторы асығады",
    "rmc_delay": "PPS → RMC кідірісі",

    "fix": "Навигациялық шешім",
    "rmc_status": "RMC күйі",
    "rmc_status_valid": "A (сенімді)",
    "rmc_status_invalid": "V (сенімсіз)",
    "fix_mode": "Шешім түрі",
    "fix_mode_none": "Шешім жоқ",
    "fix_mode_2d": "2D",
    "fix_mode_3d": "3D",
    "fix_quality": "Шешім сапасы (GGA)",
    "pdop": "PDOP",
    "hdop": "HDOP",
    "vdop": "VDOP",
    "sats": "Шешімдегі / көрінетін спутниктер",
    "lat": "Ендік",
    "lon": "Бойлық",
    "alt": "Биіктік",
    "speed": "Жылдамдық",
    "course": "Бағыт",
    "antenna": "Антенна",
    "antenna_ok": "Қалыпты",
    "antenna_open": "Үзілу",
    "antenna_short": "Қысқа тұйықталу",
    "antenna_unknown": "Белгісіз",
    "antenna_passive_hint": "Пассивті антенна ток тұтынбайды, сондықтан әрқашан «үзілу» ретінде анықталады",

    "satellites": "Спутниктер",
    "sat_system": "Жүйе",
    "sat_prn": "PRN",
    "sat_elevation": "Биіктік бұрышы",
    "sat_azimuth": "Азимут",
    "sat_snr": "С/Ш",
    "sat_used": "Шешімде",
    "sat_system_unknown": "Белгісіз",
    "no_satellites": "Көрінетін спутниктер жоқ",
    "yes": "Иә",
    "no": "Жоқ",
    "legend_strong": "Күшті сигнал: {v} дБ·Гц және одан жоғары",
    "legend_medium": "Орташа сигнал: {low}–{high} дБ·Гц",
    "legend_weak": "Әлсіз сигнал: {v} дБ·Гц-тен төмен",
    "legend_no_signal": "Сигнал жоқ",
    "legend_used": "Шешімде қолданылады",
    "legend_unused": "Шешімде қолданылмайды",
    "unplaced_caption": "Әзірге картада жоқ: қабылдағыш бұл спутниктерді естиді, бірақ олардың биіктік бұрышы мен азимутын білмейді",

    "nmea": "NMEA арнасы",
    "nmea_link_ok": "Қабылдау жүріп жатыр",
    "nmea_link_detecting": "Жылдамдық анықталуда…",
    "nmea_baud": "Жылдамдық",
    "nmea_bytes": "Қабылданған байттар",
    "nmea_sentences": "Хабарламалар",
    "nmea_checksum_errors": "Бақылау сомасы қателері",
    "nmea_rmc_valid": "Сенімді RMC",
    "nmea_rmc_invalid": "Сенімсіз RMC",

    "ntp": "NTP сервері",
    "ntp_requests": "Сұраулар",
    "ntp_answered_synced": "Синхрондалған кезде жауап берілді",
    "ntp_answered_unsynced": "Синхрондалмаған кезде жауап берілді",
    "ntp_ignored": "Еленбеді",
    "ntp_last_client": "Соңғы клиент",
    "ntp_last_request_age": "Соңғы сұраудан бері",

    "value_ms": "{v} мс",
    "value_us": "{v} мкс",
    "value_ppm": "{v} ppm",
    "value_m": "{v} м",
    "value_kmh": "{v} км/сағ",
    "value_deg": "{v}°",
    "value_dbhz": "{v} дБ·Гц",
    "value_bps": "{v} бит/с",
    "value_offset": "{v} мс ± {err} мс",
    "dur_d": "{n} күн",
    "dur_h": "{n} сағ",
    "dur_min": "{n} мин",
    "dur_s": "{n} с"
  },
  "it": {
    "title": "GPS / NTP",
    "crumbs": "Sorgente di tempo, satelliti e server NTP",
    "request_failed": "Il dispositivo non risponde: i valori sotto potrebbero non essere aggiornati",
    "request_failed_no_data": "Impossibile ottenere i dati GPS dal dispositivo",

    "time": "Ora",
    "utc_time": "Ora del dispositivo, UTC",
    "synced": "Sincronizzato",
    "not_synced": "Non sincronizzato",
    "clock_offset": "Scostamento dell'orologio di questo computer",
    "clock_offset_hint": "Come in NTP: quanto aggiungere all'orologio del computer per allinearlo al dispositivo",
    "locked_for": "Sincronizzato da",
    "lock_count": "Sincronizzazioni",
    "loss_count": "Perdite di sincronizzazione",
    "last_pps_age": "Dall'ultimo impulso PPS",
    "anchor_age": "Dall'ultimo ancoraggio (PPS + RMC)",

    "pps": "PPS",
    "pps_sub": "Impulso al secondo dal ricevitore GPS",
    "pps_count": "Impulsi ricevuti",
    "pps_interval": "Ultimo intervallo",
    "pps_interval_min": "Intervallo minimo",
    "pps_interval_max": "Intervallo massimo",
    "pps_interval_avg": "Intervallo medio",
    "crystal_error": "Errore del quarzo (dall'intervallo medio)",
    "crystal_error_hint": "Positivo: l'oscillatore del dispositivo va avanti",
    "rmc_delay": "Ritardo PPS → RMC",

    "fix": "Fix di posizione",
    "rmc_status": "Stato RMC",
    "rmc_status_valid": "A (valido)",
    "rmc_status_invalid": "V (non valido)",
    "fix_mode": "Tipo di fix",
    "fix_mode_none": "Nessun fix",
    "fix_mode_2d": "2D",
    "fix_mode_3d": "3D",
    "fix_quality": "Qualità del fix (GGA)",
    "pdop": "PDOP",
    "hdop": "HDOP",
    "vdop": "VDOP",
    "sats": "Satelliti usati / visibili",
    "lat": "Latitudine",
    "lon": "Longitudine",
    "alt": "Altitudine",
    "speed": "Velocità",
    "course": "Rotta",
    "antenna": "Antenna",
    "antenna_ok": "OK",
    "antenna_open": "Circuito aperto",
    "antenna_short": "Cortocircuito",
    "antenna_unknown": "Sconosciuto",
    "antenna_passive_hint": "Un'antenna passiva non assorbe corrente, quindi risulta sempre «aperta»",

    "satellites": "Satelliti",
    "sat_system": "Sistema",
    "sat_prn": "PRN",
    "sat_elevation": "Elevazione",
    "sat_azimuth": "Azimut",
    "sat_snr": "SNR",
    "sat_used": "Nel fix",
    "sat_system_unknown": "Sconosciuto",
    "no_satellites": "Nessun satellite visibile",
    "yes": "Sì",
    "no": "No",
    "legend_strong": "Segnale forte: {v} dB-Hz e oltre",
    "legend_medium": "Segnale medio: {low}–{high} dB-Hz",
    "legend_weak": "Segnale debole: sotto {v} dB-Hz",
    "legend_no_signal": "Nessun segnale",
    "legend_used": "Usato nel fix",
    "legend_unused": "Non usato nel fix",
    "unplaced_caption": "Non ancora sulla mappa: il ricevitore sente questi satelliti ma non ne conosce elevazione e azimut",

    "nmea": "Collegamento NMEA",
    "nmea_link_ok": "In ricezione",
    "nmea_link_detecting": "Rilevamento velocità…",
    "nmea_baud": "Velocità",
    "nmea_bytes": "Byte ricevuti",
    "nmea_sentences": "Frasi",
    "nmea_checksum_errors": "Errori di checksum",
    "nmea_rmc_valid": "RMC validi",
    "nmea_rmc_invalid": "RMC non validi",

    "ntp": "Server NTP",
    "ntp_requests": "Richieste",
    "ntp_answered_synced": "Risposte da sincronizzato",
    "ntp_answered_unsynced": "Risposte da non sincronizzato",
    "ntp_ignored": "Ignorate",
    "ntp_last_client": "Ultimo client",
    "ntp_last_request_age": "Dall'ultima richiesta",

    "value_ms": "{v} ms",
    "value_us": "{v} µs",
    "value_ppm": "{v} ppm",
    "value_m": "{v} m",
    "value_kmh": "{v} km/h",
    "value_deg": "{v}°",
    "value_dbhz": "{v} dB-Hz",
    "value_bps": "{v} bit/s",
    "value_offset": "{v} ms ± {err} ms",
    "dur_d": "{n} g",
    "dur_h": "{n} h",
    "dur_min": "{n} min",
    "dur_s": "{n} s"
  },
  "de": {
    "title": "GPS / NTP",
    "crumbs": "Zeitquelle, Satelliten und NTP-Server",
    "request_failed": "Das Gerät antwortet nicht: die Werte unten sind möglicherweise veraltet",
    "request_failed_no_data": "GPS-Daten konnten nicht vom Gerät abgerufen werden",

    "time": "Zeit",
    "utc_time": "Gerätezeit, UTC",
    "synced": "Synchronisiert",
    "not_synced": "Nicht synchronisiert",
    "clock_offset": "Uhrabweichung dieses Computers",
    "clock_offset_hint": "Wie bei NTP: so viel ist zur Uhr dieses Computers zu addieren, damit sie mit dem Gerät übereinstimmt",
    "locked_for": "Synchronisiert seit",
    "lock_count": "Synchronisierungen",
    "loss_count": "Synchronisationsverluste",
    "last_pps_age": "Seit dem letzten PPS-Impuls",
    "anchor_age": "Seit der letzten Verankerung (PPS + RMC)",

    "pps": "PPS",
    "pps_sub": "Sekundenimpulse vom GPS-Empfänger",
    "pps_count": "Empfangene Impulse",
    "pps_interval": "Letztes Intervall",
    "pps_interval_min": "Kürzestes Intervall",
    "pps_interval_max": "Längstes Intervall",
    "pps_interval_avg": "Mittleres Intervall",
    "crystal_error": "Quarzabweichung (aus dem mittleren Intervall)",
    "crystal_error_hint": "Positiv: der Oszillator des Geräts läuft zu schnell",
    "rmc_delay": "Verzögerung PPS → RMC",

    "fix": "Positionslösung",
    "rmc_status": "RMC-Status",
    "rmc_status_valid": "A (gültig)",
    "rmc_status_invalid": "V (ungültig)",
    "fix_mode": "Lösungstyp",
    "fix_mode_none": "Keine Lösung",
    "fix_mode_2d": "2D",
    "fix_mode_3d": "3D",
    "fix_quality": "Lösungsqualität (GGA)",
    "pdop": "PDOP",
    "hdop": "HDOP",
    "vdop": "VDOP",
    "sats": "Satelliten genutzt / sichtbar",
    "lat": "Breite",
    "lon": "Länge",
    "alt": "Höhe",
    "speed": "Geschwindigkeit",
    "course": "Kurs",
    "antenna": "Antenne",
    "antenna_ok": "OK",
    "antenna_open": "Unterbrechung",
    "antenna_short": "Kurzschluss",
    "antenna_unknown": "Unbekannt",
    "antenna_passive_hint": "Eine passive Antenne nimmt keinen Strom auf und wird daher immer als „Unterbrechung“ gemeldet",

    "satellites": "Satelliten",
    "sat_system": "System",
    "sat_prn": "PRN",
    "sat_elevation": "Elevation",
    "sat_azimuth": "Azimut",
    "sat_snr": "SNR",
    "sat_used": "In Lösung",
    "sat_system_unknown": "Unbekannt",
    "no_satellites": "Keine Satelliten sichtbar",
    "yes": "Ja",
    "no": "Nein",
    "legend_strong": "Starkes Signal: {v} dB-Hz und mehr",
    "legend_medium": "Mittleres Signal: {low}–{high} dB-Hz",
    "legend_weak": "Schwaches Signal: unter {v} dB-Hz",
    "legend_no_signal": "Kein Signal",
    "legend_used": "In der Lösung verwendet",
    "legend_unused": "Nicht in der Lösung verwendet",
    "unplaced_caption": "Noch nicht auf der Karte: der Empfänger hört diese Satelliten, kennt aber ihre Elevation und ihren Azimut noch nicht",

    "nmea": "NMEA-Verbindung",
    "nmea_link_ok": "Empfang läuft",
    "nmea_link_detecting": "Baudrate wird ermittelt…",
    "nmea_baud": "Baudrate",
    "nmea_bytes": "Empfangene Bytes",
    "nmea_sentences": "Sätze",
    "nmea_checksum_errors": "Prüfsummenfehler",
    "nmea_rmc_valid": "Gültige RMC",
    "nmea_rmc_invalid": "Ungültige RMC",

    "ntp": "NTP-Server",
    "ntp_requests": "Anfragen",
    "ntp_answered_synced": "Beantwortet (synchronisiert)",
    "ntp_answered_unsynced": "Beantwortet (nicht synchronisiert)",
    "ntp_ignored": "Ignoriert",
    "ntp_last_client": "Letzter Client",
    "ntp_last_request_age": "Seit der letzten Anfrage",

    "value_ms": "{v} ms",
    "value_us": "{v} µs",
    "value_ppm": "{v} ppm",
    "value_m": "{v} m",
    "value_kmh": "{v} km/h",
    "value_deg": "{v}°",
    "value_dbhz": "{v} dB-Hz",
    "value_bps": "{v} bit/s",
    "value_offset": "{v} ms ± {err} ms",
    "dur_d": "{n} T",
    "dur_h": "{n} h",
    "dur_min": "{n} min",
    "dur_s": "{n} s"
  }
}
</i18n>
