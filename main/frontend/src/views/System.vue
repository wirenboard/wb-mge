<script setup lang="ts">
import { computed, onMounted, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { changeLang, languages, type Locale } from '@/i18n';
import { useAlerts } from '@/common/alert';
import { useChannelRelease } from '@/common/channelRelease';
import { DeviceUpdateError, useFirmware } from '@/common/firmware';
import { useInfo } from '@/common/info';
import { SettingsRefreshError, useSettings } from '@/common/settings';
import { useUptime } from '@/common/uptime';
import Switch from '@/components/Switch.vue';
import type { CommandResponse, UpdateChannel } from '@/common/types';
import Button from '@/components/Button.vue';
import Configuration from '@/components/Configuration.vue';
import InfoRow from '@/components/InfoRow.vue';
import Heading from '@/components/Heading.vue';
import Info from '@/components/Info.vue';
import InputNumber from '@/components/InputNumber.vue';
import PasswordInput from '@/components/PasswordInput.vue';
import FileUpload from '@/components/FileUpload.vue';
import Layout from '@/components/Layout.vue';
import { api } from '@/utils/api';
import { onCustomValidation } from '@/utils/validation';

const { t, locale } = useI18n();
const language = ref<Locale>(locale.value as Locale);
const firmwareFile = ref<File[]>();
const loadedMethod = ref();
const { isUpdating, update } = useFirmware();
const { showAlert } = useAlerts();
const { data: settings, initData: savedSettings, isChanged, updateSettings } = useSettings();
const { isReconnecting, uptime } = useUptime();
const { info } = useInfo();
const {
  phase: updatePhase,
  progress: updateProgress,
  failure: updateFailure,
  message: updateMessage,
  notice: updateNotice,
  release,
  channels,
  isBusy: isUpdateBusy,
  canInstall,
  check: checkUpdates,
  recheck: recheckUpdates,
  install: installUpdate,
} = useChannelRelease();

const isSavingChannel = ref(false);

onMounted(() => {
  checkUpdates();
});

const offeredVersion = computed(() => (release.value?.ok ? release.value.version : ''));

// The device reports no channels at all for this signature — as opposed to a check that could not
// be performed, which gets a different sentence.
const noChannelsPublished = computed(() =>
  release.value !== null && !release.value.ok && release.value.reason === 'no-signature'
);

const isInstalledVersion = (version?: string) => !!version && version === info.value?.firmware;

// What the channel selector says about each channel. A null `channels` is a manifest that could not
// be read at all: the names are then shown bare, because a suffix would claim a lookup that never
// happened. A channel whose version could not be parsed still gets one — the manifest was read, it
// is that entry that is unusable.
const channelSuffix = (channel: UpdateChannel) => {
  if (!channels.value) {
    return '';
  }
  const version = channels.value[channel]?.version;
  // One word instead of a separate badge: the label has to fit one line of a dropdown.
  return isInstalledVersion(version)
    ? t('firmware_channel_installed', { v: version })
    : t('firmware_channel_last', { v: version ?? '—' });
};

// Whole labels rather than name + suffix: an <option> takes text, not markup.
const channelOptions = computed(() => [
  { value: 'stable' as UpdateChannel, name: t('firmware_channel_stable') },
  { value: 'testing' as UpdateChannel, name: t('firmware_channel_testing') },
].map(({ value, name }) => {
  const suffix = channelSuffix(value);
  return { value, label: suffix ? `${name} (${suffix})` : name };
}));

// isUpdating is the manual upload from the file field below. It owns the device's single
// web-server thread for up to 180 s, so a POST /settings issued meanwhile waits in that queue past
// ky's timeout: the selector would roll back and say the channel was not saved, while the device
// applies it once it reaches the request.
const isChannelLocked = computed(() => isUpdateBusy.value || isSavingChannel.value || isUpdating.value);

// In `conflict` the device is already holding a written image, so there is no update button — the
// only way forward is to look again after it has rebooted. `unavailable` is here too: it is
// reachable by a click whose pre-flight failed, and without this button the card would have no way
// out except a page reload.
const canRecheck = computed(() =>
  ['conflict', 'not_applied', 'unavailable'].includes(updatePhase.value)
);

const failureText = () => {
  if (updateFailure.value === 'no_response') {
    return t('firmware_no_response');
  }
  if (updateFailure.value === 'upload') {
    // The device names the reason it refused the image; a generic alert would send support
    // looking for a UART log.
    return updateMessage.value ?? t('wirmware_update_error');
  }
  return updateMessage.value
    ? `${t('firmware_download_failed')} (${updateMessage.value})`
    : t('firmware_download_failed');
};

const updateStatus = computed(() => {
  switch (updatePhase.value) {
    case 'checking':
      return t('firmware_checking');
    // `available` and `up_to_date` say nothing here on purpose: the version each channel offers is
    // already in the channel row, and repeating it next to the button is what made the card
    // unreadable.
    case 'unavailable':
      return noChannelsPublished.value ? t('firmware_channels_unavailable') : t('firmware_check_failed');
    case 'downloading':
      return t('firmware_downloading', { p: updateProgress.value });
    case 'uploading':
      return t('firmware_updating');
    case 'rebooting':
      return t('firmware_rebooting');
    case 'verified':
      return t('firmware_verified', { v: updateMessage.value });
    case 'not_applied':
      return t('firmware_not_applied');
    case 'conflict':
      return t('firmware_update_in_progress');
    case 'failed':
      return failureText();
    default:
      return '';
  }
});

// A click or a re-check that did not do what the user asked, shown in the status line because
// otherwise the only sign of it is a button that appears to do nothing.
const updateNoticeText = computed(() => {
  switch (updateNotice.value) {
    case 'offer_changed':
      return t('firmware_offer_changed');
    case 'recheck_failed':
      return t('firmware_recheck_failed');
    default:
      return '';
  }
});

// The channel is saved on its own request, and the offer is recomputed only from the value the
// device confirmed: updateSettings resolves after it has re-read /settings.
const onChannelChange = async () => {
  const previous = savedSettings.value?.update_channel ?? 'stable';
  isSavingChannel.value = true;
  try {
    await updateSettings({ update_channel: settings.value!.update_channel });
    await checkUpdates();
  } catch (err) {
    if (err instanceof SettingsRefreshError) {
      // The device took the channel; only reading the settings back failed. Rolling the selector
      // back here would show a channel the device no longer has — the offer is what could not be
      // recomputed, and that is what the user is told.
      console.error('Failed to re-read the settings after saving the update channel', err);
      showAlert(t('firmware_channel_recheck_failed'), { type: 'warning' });
    } else {
      console.error('Failed to save the update channel', err);
      settings.value!.update_channel = previous;
      showAlert(t('firmware_channel_save_failed'), { type: 'error' });
    }
  } finally {
    isSavingChannel.value = false;
  }
};

const startUpdate = async () => {
  if (!confirm(t('firmware_update_confirm', { v: offeredVersion.value }))) {
    return;
  }
  await installUpdate();
};

const updateFirmware = async () => {
  loadedMethod.value = 'firmware';
  showAlert(t('firmware_update_processed'), { type: 'success' });

  try {
    await update(firmwareFile.value?.[0] as File);
    location.reload();
  } catch (err) {
    firmwareFile.value = [];
    if (err instanceof DeviceUpdateError) {
      showAlert(err.message, { type: 'error' });
    } else {
      const isInProgress = (err as Error)?.message === 'update_in_progress';
      showAlert(t(isInProgress ? 'firmware_update_in_progress' : 'wirmware_update_error'), { type: 'error' });
    }
  } finally {
    loadedMethod.value = null;
  }
};

const cmd = async (command: string, confirmText?: string) => {
  if (confirmText) {
    const isConfirm = confirm(confirmText);
    if (!isConfirm) {
      return;
    }
  }

  loadedMethod.value = command;
  await api<CommandResponse>('cmd', { method: 'POST', json: { cmd: command } });
  isReconnecting.value = true;
  loadedMethod.value = null;
  setTimeout(() => {
    location.reload();
  }, 3500);
};

const updateInterface = () => {
  if (isChanged(['login', 'pass', 'web_port'])) {
    updateSettings({
      login: settings.value!.login,
      pass: settings.value!.pass,
      web_port: settings.value!.web_port,
    });
  }
  changeLang(language.value);
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <div class="grid-2">
      <div class="stack">
        <section class="card">
          <form autocomplete="off" @submit.prevent="updateSettings({ hostname: settings!.hostname })">
            <div class="card-header">
              <div class="card-title-wrap">
                <div class="title">{{ t('device_name') }}</div>
                <div class="sub">{{ t('device_name_sub') }}</div>
              </div>
              <Button
                type="submit"
                :disabled="!settings!.hostname || !isChanged(['hostname'])"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="hostname">{{ t('hostname_label') }}</label>
                <input id="hostname" v-model="settings!.hostname" type="text" class="mono" name="hostname">
              </div>
              <InfoRow :label="t('access_url_label')">
                <a class="mono muted" :href="`http://${settings!.hostname}.local`" target="_blank">http://{{ settings!.hostname }}.local</a>
              </InfoRow>
            </div>
          </form>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('device_info') }}</div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('serial_num')"><span class="mono">{{ info!.serial_num }}</span></InfoRow>
            <InfoRow :label="t('uptime')">
              <span class="muted uptime-value">
                <template v-if="uptime">
                  <template v-if="uptime.days">
                    <span>{{ t('uptime_days', { n: uptime.days }) }}</span>
                  </template>
                  <template v-if="uptime.hours">
                    <span>{{ t('uptime_hours', { n: uptime.hours }) }}</span>
                  </template>
                  <span>{{ t('uptime_minutes', { n: uptime.minutes }) }}</span>
                </template>
                <template v-else>
                  <span>—</span>
                </template>
              </span>
            </InfoRow>
            <!-- Heap memory: free, high water mark, and total -->
            <InfoRow :label="t('heap_info')">
              <span class="mono">{{ Math.round(info!.heap_free / 1024) }}({{ Math.round(info!.heap_min_free / 1024) }})/{{ Math.round(info!.heap_total / 1024) }} {{ t('kb') }}</span>
            </InfoRow>
            <!-- PSRAM availability and size detected at boot -->
            <InfoRow label="PSRAM">
              <span class="mono">
                <span v-if="info!.psram_available">{{ t('psram_available', { kb: info!.psram_size_kb }) }} {{ t('kb') }}</span>
                <span v-else>{{ t('psram_not_available') }}</span>
              </span>
            </InfoRow>
          </div>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="card-title-wrap">
              <div class="title">{{ t('power_title') }}</div>
              <div class="sub">{{ t('power_sub') }}</div>
            </div>
            <div class="power-header-controls">
              <span class="power-vout-label">V<sub>out</sub></span>
              <Switch
                id="system_vout"
                v-model="settings!.vout"
                :aria-label="t('vout_aria')"
                @change="() => updateSettings({ vout: settings!.vout })"
              />
            </div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('power')"><span class="mono">{{ info?.system_voltage?.toFixed(1) }} {{ t('v') }}</span></InfoRow>
          </div>
        </section>
</div>

      <div class="stack">
        <!-- Web interface card moved to the top of the right column -->
        <section class="card">
          <form
            :autocomplete="isChanged(['login', 'pass']) ? 'on' : 'off'"
            @submit.prevent="updateInterface">
            <div class="card-header">
              <div class="title">{{ t('interface') }}</div>
              <Button
                type="submit"
                :disabled="!isChanged(['login', 'pass', 'web_port']) && language === locale"
              >
                {{ t('save') }}
              </Button>
            </div>
            <div class="card-body">
              <div class="field">
                <label for="port">{{ t('port') }}</label>
                <InputNumber id="port" v-model="settings!.web_port" type="text" name="port" min="1" max="65535" required />
              </div>
              <div class="field">
                <label for="username">{{ t('login') }}</label>
                <input
                  id="username"
                  v-model="settings!.login"
                  type="text"
                  pattern="^[a-zA-Z0-9_\-]+$"
                  maxlength="31"
                  name="username"
                  :autocomplete="isChanged(['login']) ? 'username' : 'off'"
                  required
                  @input="(ev) => onCustomValidation(ev, t('wrong_username_pattern'))"
                />
              </div>
              <div class="field">
                <label for="new-password">{{ t('password') }}</label>
                <PasswordInput
                  id="new-password"
                  v-model="settings!.pass"
                  :placeholder="t('pass_placeholder')"
                  :autocomplete="isChanged(['pass']) ? 'new-password' : 'off'"
                  name="new-password"
                  pattern="^[\x20-\x7E]+$"
                  minlength="1"
                  maxlength="31"
                  required
                  @input="(ev: Event) => onCustomValidation(ev, t('wrong_password_pattern'))"
                />
              </div>
              <div class="field">
                <label for="language">{{ t('language') }}</label>
                <select id="language" v-model="language" name="language">
                  <option v-for="(lang, code) in languages" :key="code" :value="code">{{ lang }}</option>
                </select>
              </div>
            </div>
          </form>
        </section>

        <section class="card">
          <div class="card-header">
            <div class="title">{{ t('firmware') }}</div>
          </div>
          <div class="card-body">
            <InfoRow :label="t('firmware_current')">
              <span class="mono">{{ info?.firmware }}</span>
            </InfoRow>
            <InfoRow :label="t('firmware_channel')">
              <select
                id="update_channel"
                v-model="settings!.update_channel"
                name="update_channel"
                :disabled="isChannelLocked"
                @change="onChannelChange"
              >
                <!-- An <option> renders text only, so the version is part of the label built in the
                     script: the closed dropdown then shows the offer of the selected channel, and
                     opening it shows the other one without a second row in the card. -->
                <option v-for="option in channelOptions" :key="option.value" :value="option.value">{{ option.label }}</option>
              </select>
            </InfoRow>
            <InfoRow :label="t('firmware_update_row')">
              <div class="firmware-update">
                <div class="firmware-update-actions">
                  <!-- The label names no version: the offered one is in the channel row above, and
                       the confirm dialog still spells it out before anything is downloaded.
                       isUpdating is the manual file upload: it owns the device for up to 180 s, and
                       the pre-flight requests this button makes would queue behind it and time out,
                       leaving the card stuck in "check unavailable". -->
                  <Button
                    v-if="canInstall"
                    type="button"
                    variant="primary"
                    :disabled="isUpdateBusy || isUpdating"
                    @click="startUpdate"
                  >
                    {{ t('firmware_update_to_channel') }}
                  </Button>
                  <FileUpload
                    v-model="firmwareFile"
                    :placeholder="t('firmware_install_from_file')"
                    accept=".bin"
                    :uploading-placeholder="isUpdating ? t('firmware_updating') : t('update')"
                    :is-loading="isUpdating"
                    :disabled="loadedMethod === 'firmware' || isUpdateBusy"
                    @upload="updateFirmware"
                  />
                </div>
                <div v-if="updateStatus || updateNoticeText || canRecheck" class="firmware-update-status">
                  <span v-if="updateStatus" class="muted">{{ updateStatus }}</span>
                  <span v-if="updateNoticeText" class="firmware-notice">{{ updateNoticeText }}</span>
                  <!-- Locked during a manual upload for the same reason as the button above: it
                       re-reads /info and /settings, and those would queue behind the upload. -->
                  <Button
                    v-if="canRecheck"
                    type="button"
                    variant="outline"
                    :disabled="isUpdating"
                    @click="recheckUpdates"
                  >
                    {{ t('firmware_recheck') }}
                  </Button>
                </div>
              </div>
              <!-- Guard v-if on the template itself to avoid rendering an empty hint container -->
              <template v-if="firmwareFile" #hint>
                <Info :text="t('wirmware_update_info')" />
              </template>
            </InfoRow>
            <InfoRow :label="t('reboot')">
              <Button type="button" variant="danger" :disabled="loadedMethod === 'reboot'" @click="cmd('reboot', t('reboot_confirm'))">{{ t('restart') }}</Button>
            </InfoRow>
          </div>
        </section>

        <Configuration :cmd="cmd" :loaded-method="loadedMethod" />
      </div>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
.firmware-update {
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  gap: 6px;
}

.firmware-update-actions,
.firmware-update-status {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
  justify-content: flex-end;
}

.firmware-update-status {
  font-size: 11.5px;
}

.system-saveWrapper {
  display: flex;
  gap: 6px;
  justify-content: flex-end;
}

.system-save {
  width: 12px;
  height: 12px;
}

.uptime-value {
  display: flex;
  gap: 4px;
}

.power-header-controls {
  display: flex;
  align-items: center;
  gap: 10px;
}

.power-vout-label {
  font-size: 12px;
  color: var(--text-secondary);
}

.firmware-notice {
  color: var(--warn);
}
</style>

<i18n>
{
  "en": {
    "title": "System",
    "crumbs": "Device & maintenance",
    "device_name": "Device name",
    "device_name_sub": "Used as hostname and mDNS name on the local network",
    "device_info": "Device information",
    "firmware": "Firmware",
    "hostname_label": "Name",
    "access_url_label": "Access URL",
    "serial_num": "Serial number",
    "power_title": "Power",
    "power_sub": "Supply & auxiliary output",
    "power": "Supply voltage",
    "vout_aria": "Vout power output",
    "v": "V",
    "uptime": "Uptime",
    "uptime_days": "- | {n} day | {n} days | {n} days",
    "uptime_hours": "less than an hour | {n} hour | {n} hours | {n} hours",
    "uptime_minutes": "minute | {n} minute | {n} minutes | {n} minutes",
    "interface": "Web interface",
    "language": "Language",
    "firmware_current": "Current version",
    "firmware_check_failed": "update check unavailable",
    "firmware_channel": "Update channel",
    "firmware_channel_stable": "Stable",
    "firmware_channel_testing": "Testing",
    "firmware_channel_last": "last {v}",
    "firmware_channel_installed": "installed {v}",
    "firmware_checking": "checking for updates…",
    "firmware_channels_unavailable": "no update channels published for this board yet",
    "firmware_update_row": "Update",
    "firmware_update_to_channel": "Update to last version on channel",
    "firmware_update_confirm": "Install firmware {v}? The device will reboot.",
    "firmware_downloading": "downloading… {p}%",
    "firmware_rebooting": "the device is rebooting…",
    "firmware_verified": "updated to {v}",
    "firmware_not_applied": "the version did not change — the update was not applied",
    "firmware_download_failed": "could not download the firmware",
    "firmware_no_response": "the device did not come back online",
    "firmware_channel_save_failed": "Could not save the update channel",
    "firmware_channel_recheck_failed": "Channel saved, but the update offer could not be refreshed",
    "firmware_offer_changed": "the available version changed — check it and press update again",
    "firmware_recheck_failed": "could not re-read the device state, try again",
    "firmware_recheck": "Check state",
    "firmware_install_from_file": "Install from file",
    "wirmware_update_info": "The device will reboot after the update",
    "firmware_update_processed": "Firmware update in progress",
    "wirmware_update_error": "Firmware update error",
    "firmware_update_in_progress": "An update is already running, the device will reboot shortly",
    "update": "Update",
    "firmware_updating": "Updating",
    "reboot": "Reboot",
    "restart": "Reboot device",
    "reboot_confirm": "Are you sure you want to reboot the device?",
    "links": "Links",
    "documentation": "Documentation",
    "support": "Support",
    "website": "Buy devices",
    "firmware_link": "Download latest firmware",
    "wrong_username_pattern": "Use only Latin letters, numbers, hyphens, and underscores",
    "wrong_password_pattern": "Use only Latin letters, numbers, spaces, and special characters",
    "heap_info": "Heap used(hwm)/all",
    "kb": "KB",
    "psram_available": "{kb}",
    "psram_not_available": "Not available"
  },
  "ru": {
    "title": "Система",
    "crumbs": "Устройство и обслуживание",
    "device_name": "Имя устройства",
    "device_name_sub": "Используется как hostname и mDNS-имя в локальной сети",
    "device_info": "Информация об устройстве",
    "firmware": "Прошивка",
    "hostname_label": "Имя",
    "access_url_label": "mDNS адрес",
    "serial_num": "Серийный номер",
    "power_title": "Питание",
    "power_sub": "Питание и вспомогательный выход",
    "power": "Напряжение питания",
    "vout_aria": "Выход питания Vout",
    "v": "В",
    "uptime": "Время работы",
    "uptime_days": "- | {n} день | {n} дня | {n} дней",
    "uptime_hours": "- | {n} час | {n} часа | {n} часов",
    "uptime_minutes": "минута | {n} минута | {n} минуты | {n} минут",
    "interface": "Веб-интерфейс",
    "language": "Язык",
    "firmware_current": "Текущая версия",
    "firmware_check_failed": "проверка обновлений недоступна",
    "firmware_channel": "Канал обновлений",
    "firmware_channel_stable": "Стабильный",
    "firmware_channel_testing": "Тестовый",
    "firmware_channel_last": "последняя {v}",
    "firmware_channel_installed": "установлена {v}",
    "firmware_checking": "проверка обновлений…",
    "firmware_channels_unavailable": "для этой платы каналы обновлений пока не опубликованы",
    "firmware_update_row": "Обновление",
    "firmware_update_to_channel": "Обновить до последней версии в канале",
    "firmware_update_confirm": "Установить прошивку {v}? Устройство перезагрузится.",
    "firmware_downloading": "скачивание… {p}%",
    "firmware_rebooting": "устройство перезагружается…",
    "firmware_verified": "обновлено до {v}",
    "firmware_not_applied": "версия не изменилась — обновление не применилось",
    "firmware_download_failed": "не удалось скачать прошивку",
    "firmware_no_response": "устройство не вернулось на связь",
    "firmware_channel_save_failed": "Не удалось сохранить канал обновлений",
    "firmware_channel_recheck_failed": "Канал сохранён, но обновить предложение не удалось",
    "firmware_offer_changed": "предложение изменилось — проверьте версию и нажмите обновление ещё раз",
    "firmware_recheck_failed": "не удалось перечитать состояние устройства, попробуйте ещё раз",
    "firmware_recheck": "Проверить состояние",
    "firmware_install_from_file": "Установить из файла",
    "wirmware_update_info": "После обновления устройство будет перезагружено",
    "firmware_update_processed": "Обновление ПО в процессе",
    "wirmware_update_error": "Ошибка обновления прошивки",
    "firmware_update_in_progress": "Обновление уже идёт, устройство скоро перезагрузится",
    "update": "Обновить",
    "firmware_updating": "Обновление",
    "reboot": "Перезагрузка",
    "restart": "Перезагрузить",
    "reboot_confirm": "Вы уверены, что хотите перезагрузить устройство?",
    "links": "Ссылки",
    "documentation": "Документация",
    "support": "Техподдержка",
    "website": "Купить устройства",
    "firmware_link": "Скачать последнюю прошивку",
    "wrong_username_pattern": "Используйте только латиницу, цифры, дефисы и нижние подчеркивания",
    "wrong_password_pattern": "Используйте только латиницу, цифры, пробелы и спецсимволы",
    "heap_info": "Память свободная(минимум)/всего",
    "kb": "КБ",
    "psram_available": "{kb}",
    "psram_not_available": "Недоступна"
  },
  "kk": {
    "title": "Жүйе",
    "crumbs": "Құрылғы және қызмет көрсету",
    "device_name": "Құрылғы атауы",
    "device_name_sub": "Жергілікті желіде hostname және mDNS атауы ретінде қолданылады",
    "device_info": "Құрылғы туралы ақпарат",
    "firmware": "Микробағдарлама",
    "hostname_label": "Атауы",
    "access_url_label": "Қол жеткізу URL",
    "serial_num": "Сериялық нөмір",
    "power_title": "Қуат",
    "power_sub": "Қуат және көмекші шығыс",
    "power": "Қорек кернеуі",
    "vout_aria": "Vout қуат шығысы",
    "v": "В",
    "uptime": "Жұмыс уақыты",
    "uptime_days": "- | {n} күн | {n} күн | {n} күн",
    "uptime_hours": "бір сағаттан аз | {n} сағат | {n} сағат | {n} сағат",
    "uptime_minutes": "минут | {n} минут | {n} минут | {n} минут",
    "interface": "Веб-интерфейс",
    "language": "Тіл",
    "firmware_current": "Ағымдағы нұсқа",
    "firmware_check_failed": "жаңарту тексерісі қолжетімсіз",
    "firmware_channel": "Жаңарту арнасы",
    "firmware_channel_stable": "Тұрақты",
    "firmware_channel_testing": "Сынақ",
    "firmware_channel_last": "соңғысы {v}",
    "firmware_channel_installed": "орнатылған {v}",
    "firmware_checking": "жаңартулар тексерілуде…",
    "firmware_channels_unavailable": "бұл тақта үшін жаңарту арналары әзірге жарияланбаған",
    "firmware_update_row": "Жаңарту",
    "firmware_update_to_channel": "Арнадағы соңғы нұсқаға жаңарту",
    "firmware_update_confirm": "{v} микробағдарламасын орнату керек пе? Құрылғы қайта жүктеледі.",
    "firmware_downloading": "жүктелуде… {p}%",
    "firmware_rebooting": "құрылғы қайта жүктелуде…",
    "firmware_verified": "{v} нұсқасына жаңартылды",
    "firmware_not_applied": "нұсқа өзгерген жоқ — жаңарту қолданылмады",
    "firmware_download_failed": "микробағдарламаны жүктеу мүмкін болмады",
    "firmware_no_response": "құрылғы байланысқа оралмады",
    "firmware_channel_save_failed": "Жаңарту арнасын сақтау мүмкін болмады",
    "firmware_channel_recheck_failed": "Арна сақталды, бірақ жаңарту ұсынысын жаңарту мүмкін болмады",
    "firmware_offer_changed": "қолжетімді нұсқа өзгерді — тексеріп, жаңартуды қайта басыңыз",
    "firmware_recheck_failed": "құрылғы күйін қайта оқу мүмкін болмады, қайталап көріңіз",
    "firmware_recheck": "Күйін тексеру",
    "firmware_install_from_file": "Файлдан орнату",
    "wirmware_update_info": "Жаңартудан кейін құрылғы қайта жүктеледі",
    "firmware_update_processed": "Микробағдарламаны жаңарту жүріп жатыр",
    "wirmware_update_error": "Микробағдарламаны жаңарту қатесі",
    "firmware_update_in_progress": "Жаңарту басталып қойды, құрылғы жақында қайта жүктеледі",
    "update": "Жаңарту",
    "firmware_updating": "Жаңартылуда",
    "reboot": "Қайта жүктеу",
    "restart": "Құрылғыны қайта жүктеу",
    "reboot_confirm": "Құрылғыны қайта жүктегіңіз келетініне сенімдісіз бе?",
    "links": "Сілтемелер",
    "documentation": "Құжаттама",
    "support": "Қолдау",
    "website": "Құрылғыларды сатып алу",
    "firmware_link": "Соңғы микробағдарламаны жүктеу",
    "wrong_username_pattern": "Тек латын әріптері, сандар, дефис және астыңғы сызықша қолданыңыз",
    "wrong_password_pattern": "Тек латын әріптері, сандар, бос орындар және арнайы таңбалар қолданыңыз",
    "heap_info": "Жад своб(мин)/барлығы",
    "kb": "КБ",
    "psram_available": "{kb}",
    "psram_not_available": "Қол жетімсіз"
  },
  "it": {
    "title": "Sistema",
    "crumbs": "Dispositivo e manutenzione",
    "device_name": "Nome dispositivo",
    "device_name_sub": "Usato come hostname e nome mDNS nella rete locale",
    "device_info": "Informazioni dispositivo",
    "firmware": "Firmware",
    "hostname_label": "Nome",
    "access_url_label": "URL di accesso",
    "serial_num": "Numero di serie",
    "power_title": "Alimentazione",
    "power_sub": "Alimentazione e uscita ausiliaria",
    "power": "Tensione di alimentazione",
    "vout_aria": "Uscita di alimentazione Vout",
    "v": "V",
    "uptime": "Tempo di attività",
    "uptime_days": "- | {n} giorno | {n} giorni | {n} giorni",
    "uptime_hours": "meno di un'ora | {n} ora | {n} ore | {n} ore",
    "uptime_minutes": "minuto | {n} minuto | {n} minuti | {n} minuti",
    "interface": "Interfaccia web",
    "language": "Lingua",
    "firmware_current": "Versione attuale",
    "firmware_check_failed": "controllo aggiornamenti non disponibile",
    "firmware_channel": "Canale di aggiornamento",
    "firmware_channel_stable": "Stabile",
    "firmware_channel_testing": "Test",
    "firmware_channel_last": "ultima {v}",
    "firmware_channel_installed": "installata {v}",
    "firmware_checking": "controllo aggiornamenti…",
    "firmware_channels_unavailable": "per questa scheda i canali di aggiornamento non sono ancora pubblicati",
    "firmware_update_row": "Aggiornamento",
    "firmware_update_to_channel": "Aggiorna all'ultima versione del canale",
    "firmware_update_confirm": "Installare il firmware {v}? Il dispositivo si riavvierà.",
    "firmware_downloading": "download… {p}%",
    "firmware_rebooting": "il dispositivo si sta riavviando…",
    "firmware_verified": "aggiornato a {v}",
    "firmware_not_applied": "la versione non è cambiata — l'aggiornamento non è stato applicato",
    "firmware_download_failed": "impossibile scaricare il firmware",
    "firmware_no_response": "il dispositivo non è tornato online",
    "firmware_channel_save_failed": "Impossibile salvare il canale di aggiornamento",
    "firmware_channel_recheck_failed": "Canale salvato, ma non è stato possibile aggiornare la proposta",
    "firmware_offer_changed": "la versione disponibile è cambiata — controllala e premi di nuovo Aggiorna",
    "firmware_recheck_failed": "impossibile rileggere lo stato del dispositivo, riprova",
    "firmware_recheck": "Verifica stato",
    "firmware_install_from_file": "Installa da file",
    "wirmware_update_info": "Il dispositivo si riavvierà dopo l'aggiornamento",
    "firmware_update_processed": "Aggiornamento firmware in corso",
    "wirmware_update_error": "Errore di aggiornamento firmware",
    "firmware_update_in_progress": "Un aggiornamento è già in corso, il dispositivo si riavvierà a breve",
    "update": "Aggiorna",
    "firmware_updating": "Aggiornamento",
    "reboot": "Riavvia",
    "restart": "Riavvia dispositivo",
    "reboot_confirm": "Sei sicuro di voler riavviare il dispositivo?",
    "links": "Link",
    "documentation": "Documentazione",
    "support": "Supporto",
    "website": "Acquista dispositivi",
    "firmware_link": "Scarica l'ultimo firmware",
    "wrong_username_pattern": "Usa solo lettere latine, numeri, trattini e underscore",
    "wrong_password_pattern": "Usa solo lettere latine, numeri, spazi e caratteri speciali",
    "heap_info": "Heap lib(min)/tot",
    "kb": "KB",
    "psram_available": "{kb}",
    "psram_not_available": "Non disponibile"
  },
  "de": {
    "title": "System",
    "crumbs": "Gerät und Wartung",
    "device_name": "Gerätename",
    "device_name_sub": "Wird als Hostname und mDNS-Name im lokalen Netzwerk verwendet",
    "device_info": "Geräteinformationen",
    "firmware": "Firmware",
    "hostname_label": "Name",
    "access_url_label": "Zugriffs-URL",
    "serial_num": "Seriennummer",
    "power_title": "Stromversorgung",
    "power_sub": "Versorgung und Hilfsausgang",
    "power": "Versorgungsspannung",
    "vout_aria": "Vout-Stromausgang",
    "v": "V",
    "uptime": "Betriebszeit",
    "uptime_days": "- | {n} Tag | {n} Tage | {n} Tage",
    "uptime_hours": "weniger als eine Stunde | {n} Stunde | {n} Stunden | {n} Stunden",
    "uptime_minutes": "Minute | {n} Minute | {n} Minuten | {n} Minuten",
    "interface": "Weboberfläche",
    "language": "Sprache",
    "firmware_current": "Aktuelle Version",
    "firmware_check_failed": "Update-Prüfung nicht verfügbar",
    "firmware_channel": "Update-Kanal",
    "firmware_channel_stable": "Stabil",
    "firmware_channel_testing": "Test",
    "firmware_channel_last": "neueste {v}",
    "firmware_channel_installed": "installiert {v}",
    "firmware_checking": "Update-Prüfung läuft…",
    "firmware_channels_unavailable": "für diese Platine sind noch keine Update-Kanäle veröffentlicht",
    "firmware_update_row": "Update",
    "firmware_update_to_channel": "Auf die neueste Version im Kanal aktualisieren",
    "firmware_update_confirm": "Firmware {v} installieren? Das Gerät wird neu gestartet.",
    "firmware_downloading": "Download… {p}%",
    "firmware_rebooting": "Das Gerät startet neu…",
    "firmware_verified": "auf {v} aktualisiert",
    "firmware_not_applied": "die Version hat sich nicht geändert — das Update wurde nicht angewendet",
    "firmware_download_failed": "Firmware konnte nicht heruntergeladen werden",
    "firmware_no_response": "das Gerät ist nicht wieder online gegangen",
    "firmware_channel_save_failed": "Update-Kanal konnte nicht gespeichert werden",
    "firmware_channel_recheck_failed": "Kanal gespeichert, das Update-Angebot konnte aber nicht aktualisiert werden",
    "firmware_offer_changed": "die verfügbare Version hat sich geändert — prüfen Sie sie und drücken Sie erneut Aktualisieren",
    "firmware_recheck_failed": "Gerätestatus konnte nicht erneut gelesen werden, bitte erneut versuchen",
    "firmware_recheck": "Status prüfen",
    "firmware_install_from_file": "Aus Datei installieren",
    "wirmware_update_info": "Das Gerät wird nach dem Update neu gestartet",
    "firmware_update_processed": "Firmware-Update läuft",
    "wirmware_update_error": "Fehler beim Firmware-Update",
    "firmware_update_in_progress": "Ein Update läuft bereits, das Gerät startet in Kürze neu",
    "update": "Aktualisieren",
    "firmware_updating": "Wird aktualisiert",
    "reboot": "Neustart",
    "restart": "Gerät neu starten",
    "reboot_confirm": "Möchten Sie das Gerät wirklich neu starten?",
    "links": "Links",
    "documentation": "Dokumentation",
    "support": "Support",
    "website": "Geräte kaufen",
    "firmware_link": "Neueste Firmware herunterladen",
    "wrong_username_pattern": "Nur lateinische Buchstaben, Zahlen, Bindestriche und Unterstriche verwenden",
    "wrong_password_pattern": "Nur lateinische Buchstaben, Zahlen, Leerzeichen und Sonderzeichen verwenden",
    "heap_info": "Heap frei(Min)/ges",
    "kb": "KB",
    "psram_available": "{kb}",
    "psram_not_available": "Nicht verfügbar"
  }
}
</i18n>
