<script setup lang="ts">
import { computed, ref } from 'vue';
import { useI18n } from 'vue-i18n';
import { useAlerts } from '@/common/alert';
import { useInfo } from '@/common/info';
import { useSettings } from '@/common/settings';
import type { AirzoneProduct, CommandResponse } from '@/common/types';
import { api } from '@/utils/api';
import Button from '@/components/Button.vue';
import Heading from '@/components/Heading.vue';
import InfoRow from '@/components/InfoRow.vue';
import InputNumber from '@/components/InputNumber.vue';
import Layout from '@/components/Layout.vue';

const { t } = useI18n();
const { data, isChanged, isLoading, updateSettings } = useSettings();
const { info } = useInfo();
const { showAlert } = useAlerts();

// The line speed travels as a multiplier of 1200 baud; the installer is shown the baud it means.
const BAUD_CODES = [4, 8, 16, 32, 48, 96];
const baudOf = (code: number): number => code * 1200;

const PRODUCT_OPTIONS: AirzoneProduct[] = [0, 1, 2, 3];

const LINK_KEYS = ['link_0', 'link_1', 'link_2', 'link_3'];
const SCALE_KEYS = ['scale_0', 'scale_1', 'scale_2'];
const SETPOINT_KEYS = ['setpoint_0', 'setpoint_1', 'setpoint_2'];
const PRODUCT_KEYS = ['product_0', 'product_1', 'product_2', 'product_3'];

// The board rewrites its state every 10 s while it is alive, so anything older than three
// refreshes is a reading that stopped coming, not merely the second-newest one.
const STALE_AGE_S = 30;

// A code this build has no wording for is shown as the bare number: naming it after some
// neighbouring state would be a guess, while the raw code is still something support can act on.
const decode = (keys: string[], code: number): string => (keys[code] ? t(keys[code]) : String(code));

const airzone = computed(() => data.value?.airzone);
const gw = computed(() => info.value?.airzone_gw);

// Everything below `valid` is meaningless until the board has written once, and its zeros are
// indistinguishable from a real reading — which is exactly what this panel exists to tell apart.
const isReported = computed(() => gw.value?.valid === true);
const isStale = computed(() => isReported.value && (gw.value?.age_s ?? 0) > STALE_AGE_S);

const deadRunsHex = computed(() =>
  `0x${(gw.value?.dead_runs ?? 0).toString(16).toUpperCase().padStart(4, '0')}`
);

const isInclusionRequested = ref(false);

const save = () => {
  updateSettings({ airzone: data.value!.airzone });
};

const requestInclusion = async () => {
  isInclusionRequested.value = true;
  try {
    await api<CommandResponse>('cmd', { method: 'POST', json: { cmd: 'zwave_include' } });
    showAlert(t('include_sent'), { type: 'success' });
  } catch (err) {
    console.error('Failed to ask the board for Z-Wave inclusion', err);
    showAlert(t('include_failed'), { type: 'error' });
  } finally {
    isInclusionRequested.value = false;
  }
};
</script>

<template>
  <Layout>
    <Heading :title="t('title')" :crumbs="t('crumbs')" />

    <div class="main-body">
      <div class="grid-2">
        <div class="stack">
          <section class="card">
            <form @submit.prevent="save">
              <div class="card-header">
                <div class="card-title-wrap">
                  <div class="title">{{ t('settings') }}</div>
                  <div class="sub">{{ t('settings_sub') }}</div>
                </div>
                <Button
                  type="submit"
                  :is-loading="isLoading && isChanged(['airzone'])"
                  :disabled="!airzone || isLoading || !isChanged(['airzone'])"
                >
                  {{ t('save') }}
                </Button>
              </div>
              <div class="card-body">
                <template v-if="airzone">
                  <div class="field">
                    <label for="airzone-slave">{{ t('slave') }}</label>
                    <InputNumber id="airzone-slave" v-model="airzone!.slave" name="airzone-slave" min="1" max="247" required />
                  </div>
                  <div class="field">
                    <label for="airzone-zone">{{ t('zone') }}</label>
                    <InputNumber id="airzone-zone" v-model="airzone!.zone" name="airzone-zone" min="1" max="32" required />
                  </div>
                  <div class="field">
                    <label for="airzone-baud">{{ t('baud') }}</label>
                    <select id="airzone-baud" v-model="airzone!.baud_code" name="airzone-baud">
                      <option v-for="code in BAUD_CODES" :key="code" :value="code">{{ baudOf(code) }}</option>
                    </select>
                  </div>
                  <div class="field">
                    <label for="airzone-product">{{ t('product') }}</label>
                    <select id="airzone-product" v-model="airzone!.product" name="airzone-product">
                      <option v-for="item in PRODUCT_OPTIONS" :key="item" :value="item">{{ t(PRODUCT_KEYS[item]) }}</option>
                    </select>
                  </div>
                </template>
                <p v-else class="az-note">{{ t('settings_unavailable') }}</p>
              </div>
            </form>
          </section>

          <section class="card">
            <div class="card-header">
              <div class="title">{{ t('include') }}</div>
              <Button
                type="button"
                :is-loading="isInclusionRequested"
                :disabled="isInclusionRequested"
                @click="requestInclusion"
              >
                {{ t('include_button') }}
              </Button>
            </div>
            <div class="card-body">
              <p class="az-note">{{ t('include_hint') }}</p>
            </div>
          </section>
        </div>

        <section class="card">
          <div class="card-header">
            <div class="card-title-wrap">
              <div class="title">{{ t('state') }}</div>
              <div class="sub">{{ t('state_sub') }}</div>
            </div>
          </div>
          <div class="card-body">
            <p v-if="!isReported" class="az-note">{{ t('not_reported') }}</p>
            <template v-else>
              <p v-if="isStale" class="az-stale">{{ t('stale', { s: gw!.age_s }) }}</p>

              <div class="sub-section">
                <div class="sub-section-label">{{ t('running') }}</div>
                <InfoRow :label="t('slave')"><span class="mono">{{ gw!.slave }}</span></InfoRow>
                <InfoRow :label="t('zone')"><span class="mono">{{ gw!.zone }}</span></InfoRow>
                <InfoRow :label="t('baud')">
                  <span class="mono">{{ t('baud_value', { baud: baudOf(gw!.baud_code), code: gw!.baud_code }) }}</span>
                </InfoRow>
                <InfoRow :label="t('product')">{{ decode(PRODUCT_KEYS, gw!.product) }}</InfoRow>
              </div>

              <div class="sub-section">
                <div class="sub-section-label">{{ t('diagnostics') }}</div>
                <InfoRow :label="t('link_state')">{{ decode(LINK_KEYS, gw!.link_state) }}</InfoRow>
                <InfoRow :label="t('errors')"><span class="mono">{{ gw!.errors }}</span></InfoRow>
                <InfoRow :label="t('last_exception')"><span class="mono">{{ gw!.last_exception }}</span></InfoRow>
                <InfoRow :label="t('scale')">{{ decode(SCALE_KEYS, gw!.scale) }}</InfoRow>
                <InfoRow :label="t('setpoint_model')">{{ decode(SETPOINT_KEYS, gw!.setpoint_model) }}</InfoRow>
                <InfoRow :label="t('dead_runs')"><span class="mono">{{ deadRunsHex }}</span></InfoRow>
                <InfoRow :label="t('bus_load')">
                  <span class="mono">{{ t('percent', { n: gw!.bus_load }) }}</span>
                  <template v-if="gw!.bus_load === 0" #hint>{{ t('bus_load_zero_hint') }}</template>
                </InfoRow>
              </div>
            </template>
          </div>
        </section>
      </div>
    </div>
  </Layout>
</template>

<style scoped>
.az-note {
  margin: 8px 0 4px;
  font-size: 12px;
  line-height: 1.5;
  color: var(--text-muted);
}

.az-stale {
  margin: 8px 0 4px;
  font-size: 12px;
  line-height: 1.5;
  color: var(--warn);
}
</style>

<i18n>
{
  "en": {
    "title": "Airzone gateway",
    "crumbs": "Settings for the Z-Wave board that drives the Airzone unit, and the state it reports back.",
    "settings": "Airzone connection",
    "settings_sub": "The board validates each value on its own and can refuse one while accepting the others",
    "settings_unavailable": "This firmware does not report Airzone settings.",
    "save": "Save",
    "slave": "Modbus address",
    "zone": "Zone",
    "baud": "Line speed",
    "product": "Product type",
    "product_0": "Airzone",
    "product_1": "Aidoo Pro DX / Fan coil",
    "product_2": "Aidoo Pro Air to Water",
    "product_3": "Aidoo Pro Ventilation",
    "include": "Z-Wave network",
    "include_button": "Start inclusion",
    "include_hint": "Asks the board to enter Z-Wave inclusion mode: until the node has joined a network its radio cannot be reached, so start the add-device procedure on the controller and then press this button.",
    "include_sent": "The board was asked to enter inclusion mode",
    "include_failed": "The board did not accept the inclusion request",
    "state": "Reported by the board",
    "state_sub": "Rewritten every 10 seconds while the board is alive",
    "not_reported": "The board has not reported anything yet — there is no state to show.",
    "stale": "Last report {s} s ago; the board refreshes every 10 s.",
    "running": "Settings in use",
    "diagnostics": "Diagnostics",
    "link_state": "Link",
    "link_0": "Initializing",
    "link_1": "Online",
    "link_2": "Degraded",
    "link_3": "Offline",
    "errors": "Modbus errors in a row",
    "last_exception": "Last Modbus exception",
    "scale": "Temperature scale",
    "scale_0": "Celsius",
    "scale_1": "Fahrenheit",
    "scale_2": "Not read yet",
    "setpoint_model": "Setpoint model",
    "setpoint_0": "Single",
    "setpoint_1": "Double",
    "setpoint_2": "Undetermined",
    "dead_runs": "Silent poll runs",
    "bus_load": "Bus load",
    "bus_load_zero_hint": "Zero for the first ten seconds after the board boots — not a fault.",
    "baud_value": "{baud} baud (code {code})",
    "percent": "{n} %"
  },
  "ru": {
    "title": "Шлюз Airzone",
    "crumbs": "Настройки платы Z-Wave, которая управляет кондиционером Airzone, и состояние, которое она сообщает.",
    "settings": "Подключение к Airzone",
    "settings_sub": "Плата проверяет каждое значение отдельно и может отклонить одно, приняв остальные",
    "settings_unavailable": "Эта прошивка не сообщает настройки Airzone.",
    "save": "Сохранить",
    "slave": "Modbus-адрес",
    "zone": "Зона",
    "baud": "Скорость линии",
    "product": "Тип устройства",
    "product_0": "Airzone",
    "product_1": "Aidoo Pro DX / фанкойл",
    "product_2": "Aidoo Pro Air to Water",
    "product_3": "Aidoo Pro Ventilation",
    "include": "Сеть Z-Wave",
    "include_button": "Начать включение",
    "include_hint": "Просит плату перейти в режим включения в сеть Z-Wave: пока узел не включён в сеть, до его радио не достучаться — запустите на контроллере добавление устройства и нажмите эту кнопку.",
    "include_sent": "Плате отправлен запрос на режим включения",
    "include_failed": "Плата не приняла запрос на включение",
    "state": "Сообщает плата",
    "state_sub": "Перезаписывается каждые 10 секунд, пока плата жива",
    "not_reported": "Плата ещё ничего не сообщила — показывать нечего.",
    "stale": "Последний отчёт {s} с назад; плата обновляет данные каждые 10 с.",
    "running": "Работающие настройки",
    "diagnostics": "Диагностика",
    "link_state": "Связь",
    "link_0": "Инициализация",
    "link_1": "На связи",
    "link_2": "С ошибками",
    "link_3": "Нет связи",
    "errors": "Ошибок Modbus подряд",
    "last_exception": "Последнее исключение Modbus",
    "scale": "Шкала температуры",
    "scale_0": "Цельсий",
    "scale_1": "Фаренгейт",
    "scale_2": "Ещё не прочитана",
    "setpoint_model": "Модель уставки",
    "setpoint_0": "Одинарная",
    "setpoint_1": "Двойная",
    "setpoint_2": "Не определена",
    "dead_runs": "Молчащие циклы опроса",
    "bus_load": "Загрузка шины",
    "bus_load_zero_hint": "Первые десять секунд после запуска платы — ноль, это нормально.",
    "baud_value": "{baud} бод (код {code})",
    "percent": "{n} %"
  },
  "kk": {
    "title": "Airzone шлюзі",
    "crumbs": "Airzone кондиционерін басқаратын Z-Wave тақтасының баптаулары және оның хабарлайтын күйі.",
    "settings": "Airzone қосылымы",
    "settings_sub": "Тақта әр мәнді бөлек тексереді және біреуін қабылдамай, қалғандарын қабылдай алады",
    "settings_unavailable": "Бұл микробағдарлама Airzone баптауларын хабарламайды.",
    "save": "Сақтау",
    "slave": "Modbus адресі",
    "zone": "Аймақ",
    "baud": "Желі жылдамдығы",
    "product": "Құрылғы түрі",
    "product_0": "Airzone",
    "product_1": "Aidoo Pro DX / Fan coil",
    "product_2": "Aidoo Pro Air to Water",
    "product_3": "Aidoo Pro Ventilation",
    "include": "Z-Wave желісі",
    "include_button": "Қосуды бастау",
    "include_hint": "Тақтадан Z-Wave желісіне қосылу режиміне кіруді сұрайды: түйін желіге қосылмайынша, оның радиосына қол жеткізу мүмкін емес — контроллерде құрылғы қосу рәсімін бастаңыз да, осы түймені басыңыз.",
    "include_sent": "Тақтаға қосылу режиміне кіру сұрауы жіберілді",
    "include_failed": "Тақта қосылу сұрауын қабылдамады",
    "state": "Тақта хабарлағаны",
    "state_sub": "Тақта тірі болса, әр 10 секунд сайын қайта жазылады",
    "not_reported": "Тақта әзірге ештеңе хабарламады — көрсететін күй жоқ.",
    "stale": "Соңғы хабарлама {s} с бұрын; тақта деректерді әр 10 с сайын жаңартады.",
    "running": "Қолданыстағы баптаулар",
    "diagnostics": "Диагностика",
    "link_state": "Байланыс",
    "link_0": "Іске қосылуда",
    "link_1": "Желіде",
    "link_2": "Қателермен",
    "link_3": "Байланыс жоқ",
    "errors": "Қатарынан Modbus қателері",
    "last_exception": "Соңғы Modbus ерекшелігі",
    "scale": "Температура шкаласы",
    "scale_0": "Цельсий",
    "scale_1": "Фаренгейт",
    "scale_2": "Әлі оқылмаған",
    "setpoint_model": "Уставка моделі",
    "setpoint_0": "Жалғыз",
    "setpoint_1": "Қос",
    "setpoint_2": "Анықталмаған",
    "dead_runs": "Үнсіз сұрау циклдері",
    "bus_load": "Шина жүктемесі",
    "bus_load_zero_hint": "Тақта іске қосылғаннан кейінгі алғашқы он секундта нөл — бұл ақау емес.",
    "baud_value": "{baud} бод (коды {code})",
    "percent": "{n} %"
  },
  "it": {
    "title": "Gateway Airzone",
    "crumbs": "Impostazioni della scheda Z-Wave che comanda l'unità Airzone e stato che essa riporta.",
    "settings": "Connessione Airzone",
    "settings_sub": "La scheda convalida ogni valore separatamente e può rifiutarne uno accettando gli altri",
    "settings_unavailable": "Questo firmware non riporta le impostazioni Airzone.",
    "save": "Salva",
    "slave": "Indirizzo Modbus",
    "zone": "Zona",
    "baud": "Velocità della linea",
    "product": "Tipo di prodotto",
    "product_0": "Airzone",
    "product_1": "Aidoo Pro DX / Fan coil",
    "product_2": "Aidoo Pro Air to Water",
    "product_3": "Aidoo Pro Ventilation",
    "include": "Rete Z-Wave",
    "include_button": "Avvia inclusione",
    "include_hint": "Chiede alla scheda di entrare in modalità di inclusione Z-Wave: finché il nodo non è incluso in una rete la sua radio non è raggiungibile, quindi avvia la procedura di aggiunta dispositivo sul controller e premi questo pulsante.",
    "include_sent": "Richiesta di modalità inclusione inviata alla scheda",
    "include_failed": "La scheda non ha accettato la richiesta di inclusione",
    "state": "Riportato dalla scheda",
    "state_sub": "Riscritto ogni 10 secondi finché la scheda è attiva",
    "not_reported": "La scheda non ha ancora riportato nulla — non c'è alcuno stato da mostrare.",
    "stale": "Ultimo rapporto {s} s fa; la scheda aggiorna i dati ogni 10 s.",
    "running": "Impostazioni in uso",
    "diagnostics": "Diagnostica",
    "link_state": "Collegamento",
    "link_0": "Inizializzazione",
    "link_1": "Online",
    "link_2": "Degradato",
    "link_3": "Offline",
    "errors": "Errori Modbus consecutivi",
    "last_exception": "Ultima eccezione Modbus",
    "scale": "Scala di temperatura",
    "scale_0": "Celsius",
    "scale_1": "Fahrenheit",
    "scale_2": "Non ancora letta",
    "setpoint_model": "Modello di setpoint",
    "setpoint_0": "Singolo",
    "setpoint_1": "Doppio",
    "setpoint_2": "Non determinato",
    "dead_runs": "Cicli di polling silenziosi",
    "bus_load": "Carico del bus",
    "bus_load_zero_hint": "Zero nei primi dieci secondi dopo l'avvio della scheda — non è un guasto.",
    "baud_value": "{baud} baud (codice {code})",
    "percent": "{n} %"
  },
  "de": {
    "title": "Airzone-Gateway",
    "crumbs": "Einstellungen der Z-Wave-Platine, die das Airzone-Gerät steuert, und der Zustand, den sie zurückmeldet.",
    "settings": "Airzone-Verbindung",
    "settings_sub": "Die Platine prüft jeden Wert einzeln und kann einen ablehnen, während sie die anderen übernimmt",
    "settings_unavailable": "Diese Firmware meldet keine Airzone-Einstellungen.",
    "save": "Speichern",
    "slave": "Modbus-Adresse",
    "zone": "Zone",
    "baud": "Leitungsgeschwindigkeit",
    "product": "Produkttyp",
    "product_0": "Airzone",
    "product_1": "Aidoo Pro DX / Fan coil",
    "product_2": "Aidoo Pro Air to Water",
    "product_3": "Aidoo Pro Ventilation",
    "include": "Z-Wave-Netzwerk",
    "include_button": "Inklusion starten",
    "include_hint": "Fordert die Platine auf, in den Z-Wave-Inklusionsmodus zu wechseln: solange der Knoten keinem Netzwerk beigetreten ist, ist sein Funk nicht erreichbar — starten Sie am Controller das Hinzufügen eines Geräts und drücken Sie dann diese Schaltfläche.",
    "include_sent": "Die Platine wurde aufgefordert, in den Inklusionsmodus zu wechseln",
    "include_failed": "Die Platine hat die Inklusionsanforderung nicht angenommen",
    "state": "Von der Platine gemeldet",
    "state_sub": "Wird alle 10 Sekunden neu geschrieben, solange die Platine lebt",
    "not_reported": "Die Platine hat noch nichts gemeldet — es gibt keinen Zustand anzuzeigen.",
    "stale": "Letzte Meldung vor {s} s; die Platine aktualisiert alle 10 s.",
    "running": "Verwendete Einstellungen",
    "diagnostics": "Diagnose",
    "link_state": "Verbindung",
    "link_0": "Initialisierung",
    "link_1": "Online",
    "link_2": "Beeinträchtigt",
    "link_3": "Offline",
    "errors": "Modbus-Fehler in Folge",
    "last_exception": "Letzte Modbus-Ausnahme",
    "scale": "Temperaturskala",
    "scale_0": "Celsius",
    "scale_1": "Fahrenheit",
    "scale_2": "Noch nicht gelesen",
    "setpoint_model": "Sollwertmodell",
    "setpoint_0": "Einfach",
    "setpoint_1": "Doppelt",
    "setpoint_2": "Unbestimmt",
    "dead_runs": "Stumme Abfragezyklen",
    "bus_load": "Buslast",
    "bus_load_zero_hint": "In den ersten zehn Sekunden nach dem Start der Platine null — kein Fehler.",
    "baud_value": "{baud} Baud (Code {code})",
    "percent": "{n} %"
  }
}
</i18n>
