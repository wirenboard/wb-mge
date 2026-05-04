# Протоколы автодискавери в экосистеме ESPHome / Home Assistant

## TL;DR

- **Home Assistant поддерживает два принципиально разных протокола автодискавери для ESPHome-подобных устройств: MQTT Discovery** (универсальный pub/sub-механизм через брокер, основанный на JSON-сообщениях в специальные топики `homeassistant/<component>/.../config`) **и ESPHome Native API** (бинарный protobuf-протокол поверх TCP/6053 с обнаружением через mDNS-сервис `_esphomelib._tcp.local`). Native API — это «родной», предпочтительный вариант для ESPHome: он эффективнее (~10× меньше трафика), не требует брокера, поддерживает Bluetooth-прокси и Voice Assistant, имеет встроенное end-to-end шифрование Noise.
- **Native API следует выбирать по умолчанию**, MQTT Discovery — когда нужен общий брокер для интеграции со сторонним софтом (Node-RED, ioBroker, OpenHAB), когда HA должен оставаться одним из нескольких потребителей данных, или когда сеть/VLAN не позволяют HA напрямую достучаться до устройства, но MQTT-брокер достижим обеим сторонам.
- **Большинство проблем дискавери (зомби-entity, дубликаты, "призраки", `_2`-суффиксы, `unavailable` после рестарта) связаны не с самим протоколом, а с тремя факторами: некорректным/нестабильным `unique_id`, retained-сообщениями в брокере, которые не очищаются при удалении устройства, и неработающим mDNS через VLAN/подсети.** Перед миграцией или переименованием устройства всегда сначала фиксируйте `unique_id`, удаляйте старое устройство в HA (Settings → Devices) **и** очищайте retained discovery-сообщения в брокере пустым payload.

---

## Key Findings

1. **MQTT Discovery** работает по принципу: устройство публикует JSON в специальный топик `<discovery_prefix>/<component>/[<node_id>/]<object_id>/config` (по умолчанию prefix = `homeassistant`), HA подписан на `homeassistant/#` и автоматически создаёт entities. Дискавери поддерживается двумя видами сообщений: **single-component** (по одному топику на entity) и **device-based discovery** (один топик `homeassistant/device/<id>/config` с массивом `cmps` для всех компонентов устройства — рекомендуется в актуальных версиях HA).

2. **Native API** — это TCP-протокол на порту **6053** с фреймингом protobuf-сообщений (`api.proto` — источник истины). Дискавери выполняется через mDNS-сервис `_esphomelib._tcp.local.` с TXT-записями `version`, `mac`, `board`, `platform`, `network`, `friendly_name`, `api_encryption`. HA-интеграция `esphome` использует Python-библиотеку `aioesphomeapi`, поддерживает горячий реконнект по mDNS-объявлению при перезагрузке устройства.

3. **Безопасность**: в ESPHome 2026.1.0 поддержка `password:` в `api:` была полностью удалена; теперь применяется исключительно **Noise_NNpsk0_25519_ChaChaPoly_SHA256** с 32-байтовым base64-PSK (`api: encryption: key:`).

4. **Retain-флаг — важнейший аспект MQTT Discovery**. Если сообщение `config` опубликовано без `retain: true`, после рестарта HA не «увидит» устройство до повторной публикации. Если `retain: true` — оно остаётся в брокере навсегда, и для удаления устройства необходим ещё один retained-msg с **пустым payload**.

5. **Главные подводные камни для обоих протоколов** — переименование (`esphome.name`) ведёт к смене `unique_id` и появлению дубликатов; mDNS в VLAN-сетях не работает без Avahi-reflector / mDNS repeater (UniFi, OpenWRT, pfSense); MQTT retained-сообщения создают «призраки», которые невозможно удалить через UI без рестарта HA.

---

## Details

### 1. MQTT Discovery (Home Assistant MQTT Discovery Protocol)

#### 1.1 Принцип работы

MQTT Discovery в HA работает следующим образом: при запуске интеграции MQTT, HA подписывается на wildcard-топик `<discovery_prefix>/#` (по умолчанию prefix = `homeassistant`). Любое устройство, публикующее JSON в правильно сформированный config-топик, автоматически создаёт **entity** в HA, без правок `configuration.yaml`.

Регулярное выражение, которым HA парсит топики (из `homeassistant/components/mqtt/discovery.py`):

```
(?P<component>\w+)/(?:(?P<node_id>[a-zA-Z0-9_-]+)/)?(?P<object_id>[a-zA-Z0-9_-]+)/config
```

#### 1.2 Формат топиков

```
<discovery_prefix>/<component>/[<node_id>/]<object_id>/config
```

- `<discovery_prefix>` — по умолчанию `homeassistant`, настраивается в опциях интеграции.
- `<component>` — обязательно один из supported types: `binary_sensor`, `sensor`, `switch`, `light`, `cover`, `climate`, `fan`, `button`, `number`, `select`, `text`, `lock`, `siren`, `vacuum`, `valve`, `camera`, `alarm_control_panel`, `device_tracker`, `device_trigger`, `event`, `humidifier`, `image`, `lawn_mower`, `notify`, `scene`, `tag`, `update`, `water_heater` или **`device`** (для device-based discovery).
- `<node_id>` *(опц.)* — служит только для группировки топиков на стороне устройства (HA его игнорирует для построения `entity_id`). Только символы `[a-zA-Z0-9_-]`.
- `<object_id>` — обязательная часть. **Best practice (из HA-доков):** при наличии `unique_id` в payload — задавайте `<object_id>` равным `unique_id` и опускайте `<node_id>`.

#### 1.3 Single-component payload — пример

Топик: `homeassistant/binary_sensor/garden/config`
```json
{
  "name": "Garden Motion",
  "device_class": "motion",
  "state_topic": "garden/motion/state",
  "unique_id": "garden_motion_001",
  "device": {
    "identifiers": ["my_garden_node"],
    "name": "Garden Node",
    "manufacturer": "DIY",
    "model": "ESP32-MotionV1",
    "sw_version": "1.0.0"
  },
  "availability_topic": "garden/status",
  "payload_available": "online",
  "payload_not_available": "offline"
}
```

#### 1.4 Device-based discovery payload (новый формат)

Один топик `homeassistant/device/<unique_id>/config` описывает целое устройство со всеми entity. Корневой объект **обязан** содержать `dev` (device) и `o` (origin) — эти поля недопустимы внутри `cmps`:

```json
{
  "dev": {
    "ids": "ea334450945afc",
    "name": "Kitchen",
    "mf": "Bla electronics",
    "mdl": "xya",
    "sw": "1.0",
    "sn": "ea334450945afc",
    "hw": "1.0rev2"
  },
  "o": {
    "name": "bla2mqtt",
    "sw": "2.1",
    "url": "https://bla2mqtt.example.com/support"
  },
  "cmps": {
    "temp_sensor": {
      "p": "sensor",
      "device_class": "temperature",
      "unit_of_measurement": "°C",
      "value_template": "{{ value_json.temperature }}",
      "unique_id": "temp01ae_t"
    },
    "humid_sensor": {
      "p": "sensor",
      "device_class": "humidity",
      "unit_of_measurement": "%",
      "value_template": "{{ value_json.humidity }}",
      "unique_id": "temp01ae_h"
    }
  },
  "state_topic": "sensorBedroom/state",
  "qos": 2
}
```

Ключевые отличия:
- В корне — общие поля (`dev`, `o`, `availability`, `state_topic`, `qos`, `encoding`, `command_topic`).
- `cmps` — словарь компонентов; ключ компонента используется как часть discovery-идентификатора.
- Каждый компонент **обязан** иметь `p` (platform) и `unique_id`.
- Отсутствие компонента в новой публикации **не удаляет** его. Чтобы удалить — отправьте обновление, где у этого ключа payload пустой (но всё ещё с `p`), затем через следующее обновление можно вообще убрать ключ.

#### 1.5 Обязательные и опциональные поля

**Обязательные минимум:**
- `unique_id` — **категорически обязателен** при device-based discovery; без него HA не создаст entity registry-запись и `entity_id` будет нестабилен между перезагрузками. Если два разных entity получают одинаковый `unique_id`, HA выбросит исключение.
- `state_topic` (или `command_topic` для actuator-ов) или `topic` (для camera).
- `name` — может быть `null` (помечает entity как «main feature» устройства).
- `platform` (`p`) и `device` — для device-based discovery.

**Опциональные ключевые:**
- `device` — мэппинг для группировки в device registry: `identifiers`/`ids`, `name`, `manufacturer`/`mf`, `model`/`mdl`, `model_id`/`mdl_id`, `sw_version`/`sw`, `hw_version`/`hw`, `serial_number`/`sn`, `connections`/`cns`, `configuration_url`/`cu`, `suggested_area`/`sa`, `via_device`.
- `availability_topic` / `availability` (список) / `availability_template`, `payload_available`, `payload_not_available`, `availability_mode` (`latest`/`all`/`any`).
- `device_class`, `state_class`, `unit_of_measurement`, `entity_category`, `icon`, `value_template`, `json_attributes_topic`, `json_attributes_template`, `qos`, `expire_after`, `force_update`, `enabled_by_default`.
- `~` (TOPIC_BASE) — позволяет заменить общий префикс. В любом ключе, заканчивающемся на `_topic`, символ `~` в начале/конце будет заменён на значение `~`. Сильно сокращает payload для memory-constrained устройств.
- Все ключи имеют **сокращённые варианты** (`stat_t`, `cmd_t`, `uniq_id`, `avty_t`, `pl_avail`, `dev_cla`, `unit_of_meas` и т. д. — полный список в `homeassistant/components/mqtt/abbreviations.py`).

#### 1.6 Device Registry — группировка entities

Чтобы несколько entity отображались как одно физическое устройство в HA, у каждого entity discovery payload должен содержать `device` с **одним и тем же** значением `identifiers` (или `connections` — обычно MAC). Это связывает их в device registry.

Минимально: `{ "identifiers": ["unique_device_id"], "name": "..." }`. На практике HA требует **либо** `identifiers`, **либо** `connections`, плюс `name` (с 2024-го стало обязательным для device discovery).

#### 1.7 Флаг `retain` — почему это критично

MQTT Discovery-сообщения должны быть либо **retained**, либо повторно отправляться по триггеру:

- **`retain: true`**: Брокер хранит последнее сообщение и доставляет его HA при подписке (после рестарта HA или MQTT-интеграции). Без retain после рестарта HA все entities будут `unavailable` пока устройство не переотправит конфиг.
- **Альтернатива**: устройство подписывается на `homeassistant/status` (Birth-сообщение, по умолчанию payload `online`) и при получении переотправляет discovery-payload. Так делает, например, Tasmota и многие реализации в `aioesphome`.

HA-доки официально предупреждают: **«Retained messages can create ghost entities that keep coming back»**. Это главный источник «призраков»: вы удаляете устройство, перезапускаете HA, а оно возвращается, потому что брокер при подписке снова отдал retained config-сообщение.

#### 1.8 Удаление устройства

Чтобы корректно удалить устройство из HA через MQTT Discovery, нужно опубликовать **пустой payload** в тот же config-топик, **с `retain: true`** (чтобы стереть и предыдущий retained-msg в брокере):

```bash
mosquitto_pub -h broker -t "homeassistant/light/example/config" -r -n
# или
mosquitto_pub -h broker -t "homeassistant/light/example/config" -r -m ""
```

В device-based discovery: пустой payload в `homeassistant/device/<id>/config` удалит **все** компоненты устройства; чтобы удалить только один компонент — отправьте обновление, где у этого ключа `cmps.<id>` остался только `p` (platform) и больше ничего, либо вовсе пустой объект.

**Известный баг (issue #32509):** HA-фронтенд может не обновить вид «Integrations» сразу после удаления — entity остаётся `unavailable` до рестарта HA. После рестарта появляется кнопка «Remove Entity». Workaround — явный рестарт интеграции MQTT.

#### 1.9 Availability-топики и LWT

Для корректного отображения online/offline-статуса:

```json
"availability": [
  { "topic": "device/status", "payload_available": "online", "payload_not_available": "offline" }
],
"availability_mode": "latest"
```

Устройство публикует `online` после соединения и настраивает **MQTT Last Will Testament** на брокере с payload `offline`, retain `true`. Если устройство неожиданно отвалится — брокер сам опубликует LWT, и HA пометит entity `unavailable`.

HA сам публикует **Birth/Will** в `homeassistant/status` (по умолчанию `online`/`offline`); устройство может на это подписаться и автоматически переотправить discovery-payload, когда HA перезапускается. Это рекомендуемый способ обойти проблему retained-сообщений.

#### 1.10 Подводные камни MQTT Discovery

| Проблема | Причина | Решение |
|---|---|---|
| Дубликаты `_2`, `_3` после флэша | Нестабильный `unique_id` (например, основан на IP или каждый раз генерируется) | Делать `unique_id` детерминированным (MAC + entity_name); никогда не использовать таймстампы или счётчики |
| Призраки после удаления | retained discovery-msg в брокере | `mosquitto_pub -r -n -t '<config-topic>'` для очистки; либо MQTT Explorer → Delete |
| `entity_id` не совпадает с `unique_id` | По дизайну: `entity_id` HA генерирует из `name` устройства, а `unique_id` — внутренний идентификатор для registry. См. issue #124259. | Использовать `default_entity_id`, или вручную переименовать entity в UI (это сохранится в registry благодаря `unique_id`) |
| `<object_id>` в топике игнорируется HA | По дизайну: `<object_id>` нужен только для structuring topic-tree | Явно задавать `default_entity_id` в payload |
| После рестарта HA все MQTT entity `unavailable` | Discovery без retain и без подписки на birth | Включить `retain: true` или подписаться на `homeassistant/status` |
| `Entity does not have a unique ID` ошибка | `unique_id` не задан в payload | Всегда задавать `unique_id`; без него entity нельзя редактировать/удалять через UI |
| Высокий IO на брокере при старте HA | Тысячи retained-msg одновременно реплеятся | Добавлять случайную задержку (jitter) при отправке discovery от устройств; либо использовать device-based discovery |

---

### 2. ESPHome Native API Discovery

#### 2.1 Native API — что это

Native API — это **TCP-сервер на устройстве** (порт 6053 по умолчанию), реализованный в компоненте `api:` в YAML-конфиге ESPHome. Каждое ESPHome-устройство — самостоятельный сервер; брокер не нужен; протокол — двоичный, на основе **Protocol Buffers**. Это базовая рекомендация: «ESPHome native API has many advantages over using MQTT … much more efficient: ESPHome encodes all messages in a highly optimized format with protocol buffers — for example binary sensor state messages are about 1/10 of the size».

Минимальная конфигурация:
```yaml
api:
  encryption:
    key: "BASE64_32_BYTES_KEY=="
```

#### 2.2 Wire format / Protobuf framing

Каждое сообщение имеет следующую структуру (plaintext-режим):

```
[Indicator: 0x00] [PayloadSize: VarInt] [MessageType: VarInt] [Protobuf payload]
```

Источник истины — `esphome/components/api/api.proto`. Тип сообщения — это значение опции `(id)` в proto-определении, например `HelloRequest = 1`, `HelloResponse = 2`, `ConnectRequest = 3` (кстати, ID 3 и 4 зарезервированы — раньше там были `AuthenticationRequest/Response` для устаревшего парольного логина).

В **Noise-режиме** перед обменом protobuf-сообщениями выполняется Noise-handshake `Noise_NNpsk0_25519_ChaChaPoly_SHA256` поверх TCP, и далее каждый фрейм оборачивается:
```
[Indicator: 0x01] [Encrypted Size: 2 bytes BE] [Encrypted Payload] [MAC: 16 bytes]
```
Внутри зашифрованного payload — те же `[type_high][type_low][data_length][protobuf]`.

#### 2.3 Connection sequence

```
Client ─── HelloRequest (id=1, client_info, api_version_major/minor) ───▶ Device
Client ◀── HelloResponse (id=2, api_version, name, ...) ────────────── Device
Client ─── ConnectRequest (id=3) ───────────────────────────────────▶ Device
Client ◀── ConnectResponse (id=4) ─────────────────────────────────── Device
Client ─── ListEntitiesRequest ────────────────────────────────────▶ Device
Client ◀── ListEntitiesXxxResponse * N (sensors, switches, ...) ─── Device
Client ◀── ListEntitiesDoneResponse ────────────────────────────── Device
Client ─── SubscribeStatesRequest ─────────────────────────────────▶ Device
Client ◀── XxxStateResponse (push) ──────────────────────────────── Device
                                                              ... (long-lived)
```

Если major-версия api-протокола не совпадает — немедленный `disconnect_client_`. Minor — только warning. Соединение всегда инициируется со стороны HA.

#### 2.4 mDNS / Zeroconf discovery

При наличии компонента `mdns:` (включён по умолчанию) ESPHome-нода анонсирует себя в локальном сегменте через mDNS-сервис **`_esphomelib._tcp.local.`** на порту 6053. TXT-записи (исходник: `mdns_component.cpp`):

| Поле | Смысл |
|---|---|
| `version` | версия ESPHome (например `2026.4.0`) |
| `mac` | MAC-адрес в нижнем регистре hex (без двоеточий) |
| `board` | имя board из YAML (`esp32dev`, `esp01_1m`, ...) |
| `platform` | `ESP32` / `ESP8266` / `RP2040` / `LIBRETINY-XXX` |
| `network` | `wifi` / `ethernet` / `openthread` |
| `friendly_name` | человекочитаемое имя |
| `api_encryption` | например `Noise_NNpsk0_25519_ChaChaPoly_SHA256` (присутствует если включена шифрация) |
| `project_name`, `project_version` | если задано в YAML |
| `package_import_url` | для dashboard-import |

HA-интеграция **`zeroconf`** (использует Python `python-zeroconf`) подписана на этот service-type и при появлении нового устройства генерирует discovery-flow для интеграции `esphome` с уже заполненным host/port. Ниже — реальный лог из issue #103517:

```
ZeroconfServiceInfo(ip_address=IPv4Address('192.168.50.50'), port=6053,
  hostname='led-controller-balcony.local.',
  type='_esphomelib._tcp.local.',
  properties={'version': '2023.10.3', 'mac': 'bcff4d4c2888',
              'platform': 'ESP8266', 'board': 'esp01_1m', 'network': 'wifi',
              'api_encryption': 'Noise_NNpsk0_25519_ChaChaPoly_SHA256'})
```

#### 2.5 Reconnect-логика

`aioesphomeapi` (модуль `reconnect_logic.py`) подписывается на zeroconf-обновления и при появлении PTR-записи устройства **немедленно инициирует попытку подключения**, не дожидаясь следующего шага reconnect-таймера. Это даёт быструю реакцию на ребут устройства / выход из deep sleep.

При обрыве соединения HA повторно подключается в цикле; в логах видно:
```
WARNING [aioesphomeapi.reconnect_logic] Can't connect to ESPHome API for X @ Y: 
        Error connecting to ('Y', 6053): [Errno 113] Connect call failed
```
HA продолжит ретраи, но связанные entity при разрыве переходят в `unavailable` (LWT-эквивалента в native API нет — статус определяется фактом TCP-соединения).

#### 2.6 Шифрование (Noise / api_key)

ESPHome использует **Noise_NNpsk0_25519_ChaChaPoly_SHA256**. Свойства:
- Нет сертификатов — аутентификация только через 32-байтовый PSK.
- Perfect forward secrecy — эфемерные X25519-пары для каждой сессии.
- AEAD — ChaCha20-Poly1305.

Ключ — base64-encoded 32 байта (стандартное padding). Сгенерировать:
```bash
openssl rand -base64 32
```

YAML:
```yaml
api:
  encryption:
    key: !secret api_encryption_key
```

В HA-интеграции при добавлении устройства поле «Encryption Key» — обязательно если на устройстве настроен Noise; HA не разрешит подключение без него (ошибка `DEVICE_REQUIRES_PLAINTEXT` или наоборот).

**Важные изменения по версиям:**
- ESPHome 2023.2: `password:` в `api:` помечен deprecated.
- **ESPHome 2026.1.0**: `password:` **полностью удалён**, остался только `encryption: key:`. Сборки с `password:` падают на этапе валидации. Сообщение об ошибке:
  ```
  The 'password' option has been removed in ESPHome 2026.1.0.
  Password authentication was deprecated in May 2022.
  Please migrate to encryption: ...
  ```
- API-сообщения 3 и 4 (старые `AuthenticationRequest/Response`) зарезервированы, но не используются.

**Важно**: уникальный ключ нужен **для каждого устройства** (рекомендация security best practices); хранить в `secrets.yaml`, не коммитить в git.

#### 2.7 Подводные камни Native API

| Проблема | Причина | Решение |
|---|---|---|
| Устройство не появляется в HA | mDNS не доходит до HA | Проверить `_esphomelib._tcp` через avahi-browse / dns-sd; добавить устройство вручную по IP |
| Долгая реконнекция (~1 минута) после ребута устройства | mDNS-объявление не доходит, ждём периодический retry-таймер | Включить mDNS-reflector / Avahi на роутере (см. issue #103517) |
| `Timeout waiting for response for ConnectRequest` | Часто проблема WiFi power-save или плохой сигнал | `power_save_mode: NONE` в YAML, `fast_connect: on`, статический IP |
| Слишком много клиентов | По умолчанию `max_connections: 4` (ESP32) или 1 (ESP8266) | Поднять `max_connections` в YAML; помнить, что это compile-time константа |
| Encryption key invalid | Ключ не 32 байта после base64-decode, или скопирован с пробелом | Перегенерировать через `openssl rand -base64 32`; проверить отсутствие переносов строк |
| После рестарта все sensors срабатывают как новые | Reconnect обновляет все стейты разом → motion-сенсоры триггерят false-alarm | Добавить `delayed_on: 1s` filter; использовать `restore_mode` на binary_sensor |
| `Brownout detector triggered` | Недостаточное питание ESP32 во время handshake | Качественный БП ≥1A, конденсатор 220–470µF на VCC |
| OOM при включении encryption | Noise добавляет ~9% Flash и ~5KB RAM | Отключить ненужные компоненты, перейти на ESP-IDF фреймворк |

---

### 3. Сравнение MQTT Discovery vs Native API

| Критерий | MQTT Discovery | ESPHome Native API |
|---|---|---|
| **Транспорт** | TCP/MQTT (1883/8883), pub/sub через брокер | TCP/6053 прямое соединение |
| **Сериализация** | JSON (текст) | Protocol Buffers (бинарь, ~10× компактнее) |
| **Дискавери** | Подписка HA на `homeassistant/#` | mDNS `_esphomelib._tcp.local.` |
| **Зависимости** | Требует MQTT-брокер (Mosquitto) | Самодостаточен; каждое устройство — сервер |
| **SPOF** | Брокер: упал → всё упало | Каждое устройство автономно |
| **Latency** | Зависит от QoS и сети, обычно ~50–200ms | Ниже, но deep-sleep кейсы — медленнее (см. ниже) |
| **Шифрование** | TLS на уровне MQTT (опционально) | Встроенный Noise PSK |
| **Аутентификация** | username/password брокера, ACL | 32-байт PSK (per-device) |
| **Discovery-payload** | retain + reload-сценарий | TCP-handshake + ListEntities |
| **Удаление устройства** | Empty retained payload (хитро) | Удалить интеграцию в UI |
| **Bluetooth Proxy / Voice Assistant** | Не поддерживается | Поддерживается |
| **Работа без HA** | Да, MQTT — независимый | Native API замолкает; нужен `mqtt:` или прямой контроль |
| **Совместимость с ioBroker / Node-RED / OpenHAB** | Полная | Только OpenHAB через ESPHome binding, ioBroker — да; Node-RED — нет нативно |
| **Через VLAN/подсети** | Работает (брокер посередине) | Сложно: mDNS не проходит без reflector |
| **Удобство одной кнопкой** | Нужно настроить брокер + ACL | One-click в UI |
| **Размер прошивки** | Меньше при простом сетапе | Больше (особенно с Noise) |
| **Deep-sleep сценарии** | Иногда **быстрее** (только publish, нет handshake) | Медленнее (TCP+Noise каждый раз) |
| **Долгие команды / templates** | Через value_template на стороне HA | `homeassistant.action` с capture_response |

**Когда выбирать MQTT для ESPHome:**
- ESPHome-устройство должно работать с несколькими системами одновременно (HA + ioBroker + Node-RED).
- Сеть с VLAN, где HA и устройства разнесены, а MQTT-брокер общий.
- Много deep-sleep устройств с очень коротким окном работы (PUB-once-and-die быстрее, чем full TCP-handshake).
- Устройство и HA должны выживать друг без друга (например, MQTT-driven автоматики на стороне ESP).

**Когда выбирать Native API (default):**
- Стандартный домашний сетап без VLAN или с правильно настроенным mDNS.
- Нужен Bluetooth Proxy / Voice Assistant / Improv / dashboard import.
- Не хочется поднимать и обслуживать брокер.
- Хочется минимальной задержки и минимума «магических» сообщений в логах брокера.

ESPHome-документация цитирует это прямо: «If you are connecting to Home Assistant, you may prefer to use the native API. … MQTT is a great protocol and will never be removed».

**Гибридный режим** (сравнительно новая возможность): можно одновременно держать `api:` и `mqtt:`. Это нужно, например, чтобы HA нашло устройство по MQTT-discovery и подключилось затем по Native API (`mqtt: discover_ip: true`, `mqtt: discovery: false`). Это полезно когда mDNS не работает в сети, но MQTT-брокер достижим.

---

### 4. Частые проблемы и FAQ

#### 4.1 Устройство не обнаруживается автоматически

**Native API (ESPHome):**
1. Проверить, что mDNS включён (по умолчанию да; убедитесь, что нет `mdns: { disabled: true }`).
2. Проверить, что HA и устройство в одном broadcast-домене (одна L2-сеть). Из консоли HA-OS:
   ```bash
   docker exec homeassistant avahi-browse -art _esphomelib._tcp
   ```
3. Если устройства разнесены по VLAN — нужен **mDNS-reflector**: на UniFi включить «Multicast DNS» в настройках Network; на OpenWRT/pfSense установить Avahi с конфигом `enable-reflector=yes`.
4. Workaround: вручную добавить в HA через **Settings → Devices & Services → Add Integration → ESPHome**, ввести IP и порт 6053.

**MQTT Discovery:**
1. Убедиться, что устройство публикует в правильный prefix (HA по умолчанию — `homeassistant`).
2. В HA: **Settings → Devices & Services → MQTT → Configure → Listen to a topic** → ввести `homeassistant/#` и наблюдать.
3. Проверить, что сообщение валидный JSON (типичная ошибка — кавычки экранированы шеллом).
4. Проверить ACL пользователя MQTT — у бриджа должно быть право `write` на `homeassistant/#`.

#### 4.2 Дубликаты entities

Главная причина — нестабильный `unique_id` или переименование устройства.

**ESPHome:** при изменении `esphome.name` старые entities не удаляются автоматически (см. discussion #3204):
> «Renaming an ESPHome node (changing esphome.name) results in Home Assistant creating new entities with the new prefix while old entities remain in the Entity Registry.»

Workflow:
1. **До** переименования: удалите интеграцию ESPHome для этого устройства в HA (Settings → Devices → ⋮ → Delete).
2. Перепрошейте устройство с новым `name:`.
3. Заново добавьте интеграцию.

**MQTT:** дубликаты `_2`, `_3` появляются если `unique_id` отличается от старого (опечатка, новая прошивка с другим алгоритмом генерации). Проверить registry в `core.entity_registry` (в `/config/.storage/core.entity_registry`) — там видно, какой `unique_id` у каждого entity.

#### 4.3 Зомби-устройства после переименования

Симптом: в Devices видно старое имя устройства, все entities `unavailable`, в `cmps` уже их нет.

**MQTT:** retained discovery-msg всё ещё сидит в брокере под старым топиком. Очистить:
```bash
mosquitto_sub -h broker -t 'homeassistant/#' -v --retained-only
# найти зомби-топик, затем:
mosquitto_pub -h broker -t 'homeassistant/sensor/old_node/old_object/config' -r -n
```
Или использовать **MQTT Explorer** — в нём retained-сообщения помечены и удаляются кликом.

**ESPHome native:** удалить устройство из Settings → Devices → ⋮ → Delete; затем при необходимости вычистить остатки в `.storage/core.entity_registry` и `.storage/core.device_registry` (выключив HA, отредактировать вручную, рестартовать).

#### 4.4 Проблемы `unique_id`

**Симптом:** ошибка "This entity does not have a unique ID" при попытке редактировать; entity_id не персистентный.

- **MQTT:** добавить `"unique_id": "<device_mac>_<entity_name>"` в discovery payload. Best practice: использовать MAC + имя сенсора, не имя устройства (чтобы при переименовании unique_id не менялся).
- **ESPHome:** unique_id формируется автоматически из `esphome.name` + entity_id; при переименовании устройства unique_id меняется. Issue #35683 поднимал необходимость интерполяции имени устройства во все идентификаторы — это до сих пор открыто. Workaround: всегда задавать `id:` под каждым sensor/switch и не менять его.

**Конфликт двух entities с одинаковым unique_id:** HA выбрасывает исключение и не создаёт второй entity. В логах: `An entity with id 'xxx' already exists`. Fix: изменить unique_id одного из устройств, либо удалить старое из registry.

#### 4.5 MQTT retained messages и «призраки»

Чек-лист «глубокой очистки» MQTT-discovery:
1. Установить MQTT Explorer (или `mosquitto_sub --retained-only -t 'homeassistant/#'`).
2. Найти все retained-сообщения с устаревшими топиками.
3. Удалить их (в MQTT Explorer — кликом «Delete», или `mosquitto_pub -r -n -t '<topic>'`).
4. В HA: Settings → Devices & Services → MQTT → ⋮ → Reload.
5. Если призрак остался — рестартануть HA целиком (известный баг #32509: фронтенд не обновляется до рестарта).

#### 4.6 mDNS не работает через VLAN/подсети

Это **самая частая** причина «ESPHome device offline». ESPHome-доки явно предупреждают:
> «mDNS might not work if your Home Assistant server and your ESPHome nodes are on different subnets and/or VLANs.»

Решения:
1. **Avahi-reflector** на роутере (OpenWRT, pfSense, OPNsense, EdgeRouter): в `/etc/avahi/avahi-daemon.conf`:
   ```
   [reflector]
   enable-reflector=yes
   ```
   Плюс firewall-правило: разрешить UDP/5353 на multicast-адрес 224.0.0.251 между нужными интерфейсами.
2. **UniFi**: включить «Multicast DNS» в каждой нужной сети (Settings → Networks → конкретная VLAN → Advanced → mDNS).
3. **Полный workaround**: статический IP на устройстве + ручное добавление в HA по IP. ESPHome-dashboard в этом режиме не сможет показывать online/offline через mDNS — настройте `ESPHOME_DASHBOARD_USE_PING=true` (через переменную окружения для Docker) или `status_use_ping: true`.
4. **Hybrid MQTT+API**: устройство публикует свой IP в MQTT (`mqtt: discover_ip: true`), HA читает и подключается к Native API напрямую — обходит mDNS.

#### 4.7 Что делать, если устройство не появляется в HA

Универсальный чек-лист (порядок важен):

1. **Сеть**: `ping <device_ip>` от хоста HA. Если не пингуется — проблема L2/L3 (VLAN, firewall, неверная маска).
2. **Порт**: `nc -zv <device_ip> 6053` (для Native API) или `nc -zv <broker> 1883` (для MQTT).
3. **mDNS**: `avahi-browse -art _esphomelib._tcp` или с macOS `dns-sd -B _esphomelib._tcp`.
4. **Логи устройства**: в ESPHome Builder открыть **Logs**; если устройство раздаёт state — оно живо.
5. **Логи HA**: в `configuration.yaml`:
   ```yaml
   logger:
     default: warning
     logs:
       homeassistant.components.mqtt: debug
       homeassistant.components.zeroconf: debug
       homeassistant.components.esphome: debug
       aioesphomeapi: debug
   ```
6. **Проверить ключ шифрования**: при несоответствии ключа handshake падает с явной ошибкой `Handshake MAC failure` — видно в логах HA.
7. **Ручное добавление**: Settings → Devices & Services → Add Integration → ESPHome → ввести IP и порт. Если ручное работает, а авто-discovery нет — проблема с mDNS/Zeroconf.
8. **Reload интеграции**: иногда HA «зависает» в reconnect-loop; перезагрузка интеграции (⋮ → Reload) или рестарт HA помогают.
9. **Слишком много клиентов на устройстве**: дашборд ESPHome + HA + ESPHome Device Builder logs могут исчерпать `max_connections` (по умолчанию 4 на ESP32, 1 на ESP8266). Закройте лишние логи или поднимите лимит в YAML.
10. **Проверить firewall на стороне HA**: некоторые Docker-конфигурации блокируют входящий multicast (особенно при запуске в `bridge`-сети без `host` networking).

---

## Recommendations

**Стартовая точка для нового пользователя ESPHome:**
1. **Используйте Native API** с обязательной Noise-шифрацией (`api: encryption: key:`). Это путь по умолчанию и поддерживается всеми современными фичами (Bluetooth Proxy, Voice Assistant).
2. **Уникальный 32-байтный ключ для каждого устройства**, хранить в `secrets.yaml`.
3. **Все устройства в одной L2-сети** с HA, либо настроенный Avahi-reflector. Не разносите по VLAN без необходимости.

**Если переходите на MQTT (для ESPHome или сторонних устройств):**
1. **Всегда задавайте `unique_id`** в payload, делайте его детерминированным (MAC-based).
2. **Используйте device-based discovery** (один топик `homeassistant/device/<id>/config`) вместо single-component — меньше I/O, проще удалять.
3. **`retain: true`** на discovery-топиках + подписка на `homeassistant/status` для повторной публикации после рестарта HA.
4. **LWT/availability**: каждое устройство публикует `online` после соединения и настраивает Will = `offline`, retain `true`.
5. **Регулярно (раз в полгода) аудитьте retained-сообщения** через MQTT Explorer — это предотвратит накопление призраков.

**Перед переименованием/миграцией устройства:**
1. Сначала **удалите** устройство в HA UI.
2. Для MQTT — отправьте пустой retained-payload в старый config-топик.
3. Только потом меняйте `esphome.name` и перепрошивайте.
4. После — добавьте устройство заново.

**Триггеры пересмотра выбора:**
- Если у вас > 5 устройств с deep-sleep и проблемы с надёжностью пробуждения → попробуйте MQTT.
- Если HA и устройства должны выживать друг без друга (например, парные ESPHome-сценарии без HA) → MQTT с локальной автоматикой на ESP.
- Если устройство должно интегрироваться более чем с одной системой (HA + Node-RED) → MQTT.
- Если возникают проблемы латентности или нужен Bluetooth Proxy / Voice Assistant → переходите на Native API.

**Долгосрочная гигиена:**
- Заведите соглашение о именовании: префикс по комнате, тип устройства, индекс (`livingroom-temp-01`).
- Регулярно проверяйте `Settings → Devices → Disabled entities` и чистите.
- Делайте бэкап `.storage/core.entity_registry` и `core.device_registry` перед массовыми операциями.
- Подпишитесь на ESPHome changelog: с каждым релизом меняются дефолты (как удаление password в 2026.1.0).

---

## Caveats

- **Версии API меняются**. Major-версия Native API при несовпадении приводит к немедленному дисконнекту, minor — только к warning. ESPHome 2026.1.0 удалил `password:` для API; устройства, не обновлённые до Noise, не могут подключиться к свежим HA. Эта статья отражает состояние на конец апреля – начало мая 2026 (последние подтверждённые данные — ESPHome 2026.4.0 от апреля 2026).
- **`<object_id>` в MQTT discovery topic не влияет на `entity_id`** — это часто сбивает с толку. Для контроля `entity_id` используйте поле `default_entity_id` в payload (новое поле). До его появления приходилось менять имя через UI или жить с авто-генерацией от `device.name + name`.
- **Поведение после удаления entity через MQTT Discovery** имеет хроническую проблему (см. issue #32509): UI не всегда обновляется до рестарта HA. Это может создавать впечатление, что удаление не сработало.
- **Native API не имеет аналога MQTT LWT.** Доступность в HA определяется фактом TCP-соединения; если устройство «зависло» с открытым TCP-сокетом, но не отвечает — HA узнает об этом только по таймауту (по умолчанию ~90 сек до keepalive ping fail).
- **mDNS-reflector через VLAN — это компромисс безопасности.** Включая reflector, вы пробрасываете multicast-трафик между сегментами, что может частично нивелировать смысл изоляции IoT-VLAN. Это нормальный trade-off для большинства домашних сетей, но не подходит, если IoT-сеть считается «грязной».
- **device-based discovery — относительно новая фича.** Старые библиотеки и интеграции (включая некоторые версии Zigbee2MQTT, Tasmota) до сих пор используют single-component discovery; миграционный путь в HA-доках описан, но требует ручных шагов с `migrate_discovery: true`.
- **Быстродействие сравнения «MQTT vs API при deep sleep»** зависит от прошивки, фреймворка (Arduino vs ESP-IDF), уровня сигнала WiFi и DTIM — приведённые в статье общие рекомендации могут не совпадать с вашими измерениями. Один пользователь сообщал на форуме, что «API takes at least three times as long as MQTT to wake and report». Это **не универсально** и зависит от сценария.
- **Документация ESPHome иногда отстаёт** от кода. Источник истины для проверки — `api.proto` в репозитории `esphome/esphome`, `discovery.py` и `abbreviations.py` в `home-assistant/core`.