# Тестовый стенд WB-MGE/MGU — доступ, прошивка, логи, MQTT

Справочный документ по железному стенду для отладки прошивки (в частности
mqtt-serial bridge). Не привязан к конкретной задаче — ссылайтесь на него из ТЗ.

> Стабильные факты (IP контроллера, `/dev/ttyACM0`, offset'ы, baud) меняются
> редко. Значения, помеченные «текущее», зависят от того, что сейчас на стенде
> (модель платы, устройство на шине, id топиков) — их стоит перепроверять.

---

## 1. Топология

- **Dev-машина** — где собирается прошивка (ESP-IDF). ESP к ней **не** подключён.
  - Рабочая копия (worktree) фичи mqtt-serial:
    `/Users/vvzvlad/Data/Projects/wirenboard/wb-mge/.claude/worktrees/rebase-mqtt`,
    ветка `feature/vvzvlad-mqtt-serial-micro`.
- **WB-контроллер** — Linux-плата Wiren Board, через неё идёт вся работа с ESP.
  - IP: **`10.31.41.185`**, hostname `wirenboard-AKK7LBSW`, доступ по SSH под `root`.
  - К нему по USB подключён ESP (плата WB-MGE/MGU) как **`/dev/ttyACM0`** — это
    и порт прошивки (esptool), и консоль логов ESP.
  - На контроллере есть `esptool.py` (`/usr/local/bin/esptool.py`) и
    `mosquitto_sub`/`mosquitto_pub`.
  - На контроллере крутится MQTT-брокер `mosquitto` на `localhost:1883` —
    **ESP публикует данные именно сюда**.
- **Устройство на RS485** (текущее): датчик **WB-MSW v4**, slave id **131**,
  подключён к RS485-порту ESP.

Схема: `Mac (сборка) → scp .bin → WB-контроллер → esptool по /dev/ttyACM0 → ESP`.
Данные: `ESP → RS485 → WB-MSW`, и `ESP → MQTT → mosquitto на контроллере`.

---

## 2. Доступ

Доступ по SSH-ключу уже настроен с dev-машины (заходит без пароля). Проверка:

```bash
ssh -o BatchMode=yes root@10.31.41.185 'hostname; ls -l /dev/ttyACM0; which esptool.py mosquitto_sub'
```

Ожидаемо: хост, `/dev/ttyACM0`, пути утилит. Если `BatchMode` падает — ключа нет:
запросить доступ/пароль у владельца стенда (новая плата — доступ может быть ещё
не выдан).

---

## 3. Сборка прошивки (на dev-машине)

```bash
cd /Users/vvzvlad/Data/Projects/wirenboard/wb-mge/.claude/worktrees/rebase-mqtt
make build-idf-project
```

Результат:
- `build/mge_v3.bin` — образ приложения (шьётся на `0x90000`);
- также `build/bootloader/bootloader.bin`, `build/partition_table/partition-table.bin`,
  `build/ota_data_initial.bin` (нужны только при полной прошивке);
- копия релиза: `release/mge_v3__<ver>_<branch>_<sha>.bin`.

Собирать из **своего** worktree/ветки, не из основного репозитория.

---

## 4. Прошивка (Mac → контроллер → ESP)

Меняли только код приложения → достаточно прошить **app** на `0x90000` (быстро):

```bash
# 1) с Mac: закинуть свежий бинарь на контроллер
scp -o BatchMode=yes build/mge_v3.bin root@10.31.41.185:/tmp/fw/

# 2) на контроллере: прошить app + hard reset
ssh -o BatchMode=yes -o ConnectTimeout=70 root@10.31.41.185 '
  esptool.py --chip esp32 --port /dev/ttyACM0 -b 460800 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
    0x90000 /tmp/fw/mge_v3.bin'
```

**Полная прошивка** (если менялись bootloader / таблица разделов / ota_data) —
скопировать все 4 образа и прошить по offset'ам:

| offset   | образ                                   |
|----------|-----------------------------------------|
| `0x1000` | `build/bootloader/bootloader.bin`       |
| `0x8000` | `build/partition_table/partition-table.bin` |
| `0xd000` | `build/ota_data_initial.bin`            |
| `0x90000`| `build/mge_v3.bin` (приложение)         |

```bash
scp -o BatchMode=yes build/bootloader/bootloader.bin build/partition_table/partition-table.bin \
    build/ota_data_initial.bin build/mge_v3.bin root@10.31.41.185:/tmp/fw/
ssh -o BatchMode=yes root@10.31.41.185 '
  esptool.py --chip esp32 --port /dev/ttyACM0 -b 460800 --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
    0x1000 /tmp/fw/bootloader.bin 0x8000 /tmp/fw/partition-table.bin \
    0xd000 /tmp/fw/ota_data_initial.bin 0x90000 /tmp/fw/mge_v3.bin'
```

---

## 5. Чтение консоли / логов ESP

Консоль ESP — на том же `/dev/ttyACM0`, скорость **115200**. Один порт:
**сначала прошивка, потом чтение** (одновременно держать нельзя). Загрузка
до подключения к MQTT — примерно **20–25 c**.

```bash
ssh -o BatchMode=yes -o ConnectTimeout=70 root@10.31.41.185 '
  stty -F /dev/ttyACM0 115200 raw -echo -echoe -echok
  timeout 40 cat /dev/ttyACM0'
```

Совет: писать в файл и грепать нужное (окно короткое, можно упустить разовые
строки — например лог, который выводится один раз при старте):

```bash
ssh -o BatchMode=yes -o ConnectTimeout=70 root@10.31.41.185 '
  stty -F /dev/ttyACM0 115200 raw -echo -echoe -echok
  timeout 50 cat /dev/ttyACM0 > /tmp/esp.log 2>&1
  grep -aE "mqtt_serial_bridge|Board detected|template|Guru|abort|E \(" /tmp/esp.log'
```

Полезные маркеры в логе:
- `Board detected: WB-MGU (PSRAM)` / `WB-MGE (no PSRAM)` — автоопределение платы;
- `Template: '...' (N channels)`, `Bridge started: ... slave=... -> MQTT ...`;
- `Guru Meditation` / `abort` / `rst:` — краши/перезагрузки.

---

## 6. Проверка через MQTT (на контроллере)

Формат топиков: `modbusmqtt/<gateway>/<device>/controls/<канал>`
(+ `.../status` = `online`/`offline`). Текущие: gateway `WB-MGE-3091AF`,
device `wb-msw-v4-131`.

```bash
# все значения устройства
ssh root@10.31.41.185 "timeout 15 mosquitto_sub -h localhost -t 'modbusmqtt/#' -v"

# один канал
ssh root@10.31.41.185 "timeout 15 mosquitto_sub -h localhost -t 'modbusmqtt/+/wb-msw-v4-131/controls/Illuminance' -v"

# сколько сообщений по каналу за 20 c (для оценки трафика)
ssh root@10.31.41.185 "timeout 20 mosquitto_sub -h localhost -t 'modbusmqtt/+/wb-msw-v4-131/controls/CO2' | wc -l"

# запись в устройство (команда): топик .../controls/<канал>/on
ssh root@10.31.41.185 "mosquitto_pub -h localhost -t 'modbusmqtt/WB-MGE-3091AF/wb-msw-v4-131/controls/<канал>/on' -m '<значение>'"
```

Значения WB-MSW, которые естественно меняются (удобно для проверки live-обновлений):
`Illuminance` (плавает), `Max Motion` (реагирует на движение у датчика).

---

## 7. Шина RS485 / конфиг WB

Штатный опрос WB по шине **отключён намеренно**, чтобы шина была за ESP:
в `/etc/wb-mqtt-serial.conf` порт `/dev/ttyRS485-1` стоит `enabled: false`
(бэкап — `/etc/wb-mqtt-serial.conf.bak.claude`). Во время тестов ESP —
единственный мастер на шине; **обратно не включать**.

Вернуть опрос WB (если понадобится):
```bash
ssh root@10.31.41.185 'cp /etc/wb-mqtt-serial.conf.bak.claude /etc/wb-mqtt-serial.conf && systemctl restart wb-mqtt-serial'
```

Шаблон устройства, который использует ESP, лежит у него в SPIFFS (компактный
профиль WB-MSW v4). Штатные WB-шаблоны для сверки адресов регистров:
`/usr/share/wb-mqtt-serial/templates/config-wb-msw_v4.json` на контроллере.

---

## 8. Траблшутинг

- **`/dev/ttyACM0` занят / прошивка не заходит** — висит незакрытый `cat`
  консоли. Все команды выше используют `timeout` и сами отпускают порт; убить
  зависшее: `ssh root@10.31.41.185 'fuser -k /dev/ttyACM0'`.
- **ESP «завис в download mode»** после esptool — повторить прошивку с
  `--after hard_reset` (она возвращает чип в рабочий режим), либо
  `esptool.py --port /dev/ttyACM0 --after hard_reset run`.
- **`select() timeout` / `MQTT error` в первые ~15 c после старта** — брокер
  кратко недоступен на старте, соединение восстанавливается само; к логике
  моста отношения не имеет.
- **Пустой лог при чтении консоли** — не попали в окно (разовые строки при
  старте выводятся один раз); сбросить ESP и читать сразу после сброса, либо
  увеличить `timeout`.

---

## 9. Быстрый справочник

| Что | Значение |
|-----|----------|
| Контроллер (SSH, брокер) | `root@10.31.41.185` (`wirenboard-AKK7LBSW`) |
| Порт ESP (flash + консоль) | `/dev/ttyACM0`, консоль **115200** |
| esptool | `/usr/local/bin/esptool.py` на контроллере |
| App offset | `0x90000` (полный набор: `0x1000/0x8000/0xd000/0x90000`) |
| Флаги flash | `--flash_mode dio --flash_size 4MB --flash_freq 40m`, `-b 460800` |
| MQTT | `localhost:1883` на контроллере; префикс `modbusmqtt/#` |
| Устройство на шине (текущее) | WB-MSW v4, slave 131 |
| gateway/device id (текущие) | `WB-MGE-3091AF` / `wb-msw-v4-131` |
| Плата (текущая) | WB-MGU (PSRAM), прошивка универсальная |
| Сборка | `make build-idf-project` в worktree `rebase-mqtt` |
