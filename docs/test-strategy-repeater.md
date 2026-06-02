# Отчёт по тест-стратегии — фича «Repeater» (wb-mge) — 2026-06-02

> **Область:** только незакоммиченная фича прозрачного RS-485-повторителя на ветке
> `feature/vvzvlad-caching-multimaster`. Незатронутый код прошивки в анализ не входит.
> Это отдельный отчёт; общий отчёт по репозиторию лежит в `docs/test-strategy-report.md`
> (от 2026-05-27, фичу не покрывает) и не перезаписывается.

## 1. Исполнительное резюме

- **Проанализировано модулей фичи:** 4 (repeater-core C, port_manager-интеграция C, info/API/e2e, фронтенд Vue).
- **Уже есть тестов у фичи (проверено запуском):** 10 unit (`repeater_test.c`) + 3 unit в `port_manager_test.c` + 5 integration (`Repeater.integration.test.ts`) + 1 e2e-проверка enum режимов (`13_test_ports.py`). Все зелёные.
- **Предложено новых тестов (unit / integration / contract / E2E):** **11 / 1 / 1 / 2** = 15.
  Доля: unit 73 %, integration 7 %, contract 7 %, E2E 13 % (2 абсолютных, оба с user journey).
- **Отклонено как малоценные:** ~9 целей (i18n×5 локалей, router, sidebar, DTO-типы, OpenAPI-как-артефакт, внутренности `useOptimisticToggle`, тривиальные геттеры).
- **Покрытие сейчас → прогноз после внедрения (по фиче):**
  - `repeater.c`: **88.35 % строк / 76.32 % веток** (gcov, проверено) → ~98–100 %.
  - дельта `port_manager.c` под repeater: ~55 % → ~95 %.
  - блок `repeater` в `info_handlers.c`: **0 % проверено** (исполняется, но ни один тест не читает объект) → покрыт contract + E2E.
  - фронтенд `Repeater.vue`: ~70 % поведения → ~90 %.

### Независимая проверка покрытия (Фаза 4)

| Что запускалось | Результат |
|---|---|
| `cd unittests/repeater && make` | 10/10 PASS; gcov `repeater.c` = 88.35 % строк, 76.32 % веток |
| `cd unittests/port_manager && make` | 44/44 PASS (вкл. 3 новых repeater-теста) |
| `npx vitest run src/views/Repeater.integration.test.ts` | 5/5 PASS |
| чтение `api_tests/13_test_ports.py` | enum режимов (4 валидных + отклонение `invalid`/`sniffer`/`cache_bus`) реально покрыт |

Все заявления аналитиков «уже покрыто» подтверждены фактическим прогоном. Расхождений нет.

---

## 2. Рекомендации по модулям

### Модуль 1 — repeater-core (`main/bridge/repeater.c`, тесты `unittests/repeater/`)
Чистая логика над замоканным `serial`; почти всё — unit-уровень.

- **Unit-тесты добавить:**
  - **U-R2 · двойной init (`repeater_init_port`, строки 106–110).** Init порта 0 дважды → второй вызов возвращает тот же дескриптор, `serial_init` вызван 1 раз, `s_active_count` не задвоен. *Ловит:* утечку дескриптора и двойной учёт активного порта (флаг `active` перестанет корректно сбрасываться). **Самая крупная непокрытая ветка.** Рефактор не нужен.
  - **U-R3 · неизвестный дескриптор в `repeater_rx_handler`.** Подать в захваченный handler незарегистрированный `serial_desc` → `find_index_by_serial_desc` = −1, ранний выход, `serial_send` не вызван, счётчики не тронуты. *Ловит:* выход за границы массива `s_bytes`/`s_dropped` и ложную пересылку при инверсии guard'а. Рефактор не нужен.
  - **U-R1 · идемпотентность `repeater_init`.** Первый вызов создаёт мьютекс, второй — нет. *Ловит:* регрессию, где `s_lock == NULL`-guard убран и мьютекс пересоздаётся при каждом вызове (утечка + потеря защиты живых портов). **Зависит от рефактора R1.**
  - **U-R4 · инвариант порядка блокировки в `repeater_deinit_port`.** Утверждать, что указатель обнулён под локом, а `serial_deinit` вызван **после** `xSemaphoreGive` (через `call_seq`). *Ловит:* возврат `serial_deinit` внутрь критической секции `s_lock` — задокументированный класс дедлока против UART-задачи, ждущей `s_lock` в `repeater_rx_handler`. Однопоточный тест пересылки это не ловит — нужна именно проверка порядка. **Точная версия зависит от R2;** слабая версия (баланс take/give + `deinit_called==1`) — без рефактора.
- **НЕ тестировать:** `repeater_reset_for_test` (тестовая обвязка `__unittest_env__`); `repeater_get_stats` NULL-guard (тривиальный ранний выход); `find_index_by_serial_desc` напрямую (static, покрыт транзитивно); все 10 существующих сценариев пересылки/drop/active/uptime/invalid-args — **уже покрыто** (проверено).

### Модуль 2 — port_manager-интеграция (`main/bridge/port_manager.c`, тесты `unittests/port_manager/`)
Диспетчеризация режима над моками; всё — unit.

- **Unit-тесты добавить:**
  - **U-P1 · обратный парсинг `str_to_pm_mode("repeater")` через загрузку NVS.** NVS = `"repeater"` → порт поднимается как `PM_MODE_REPEATER` (`repeater_init_port` вызван); NVS = мусор → `PM_MODE_DISABLED` (`init` не вызван). *Ловит:* регрессию обратного парсинга и контракт «неизвестное → DISABLED». **Единственный путь, доказывающий, что repeater переживает перезагрузку.**
  - **U-P5 · переходы `tcp_bridge→repeater` и `repeater→passive`.** (a) bridge-deinit + sniffer_detach, затем repeater_init + sniffer_attach + rx_timeout=PROXY; (b) repeater-deinit (через `repeater_deinit_port`, не `serial_deinit`) + bridge_init_serial_only + rx_timeout=SNIFFER. *Ловит:* неверную диспетчеризацию deinit при входе/выходе из repeater из режима, отличного от passive (существующий тест покрывает только `passive→repeater→disabled`).
  - **U-P6 · повторный `set_mode(REPEATER)` (идемпотентность на уровне PM).** Второй вызов = ровно один цикл deinit→init, без двойного init. *Ловит:* «UART уже установлен»-регрессию на уровне диспетчера (та самая гонка, ради которой существует `init_mutex`).
  - **U-P4 · сброс статистики при teardown repeater.** При `repeater→disabled` вызваны `rs485_busy_monitor_reset` и `rs485_stats_reset` (помимо уже покрытых sniffer_detach + repeater_deinit). *Ловит:* утечку устаревшей RS-485-статистики между сменами режима.
  - **U-P3 · `get_port_serial_desc(REPEATER)` через `port_manager_send_raw`.** *Ловит:* возврат NULL для repeater-ветки (тихо ломает `/ports/N/send` и sniffer/cache-оверлей для repeater-порта).
  - **U-P2 · `check_settings_changed(REPEATER)`, под-ветка смены режима.** repeater→passive в NVS → `true`; без изменений → `false`. *Ловит:* repeater не подключён к детекту изменений (settings_update_task никогда не переприменит repeater→другое → залипший порт). Под-ветка смены **серийных параметров** заблокирована мок-ограничением (см. R3).
- **НЕ тестировать:** порядок вызова `repeater_init()` в `port_manager_init()` (lifecycle-обвязка, пустой мок-стаб, нет наблюдаемого поведения); `pm_lock`/`cache_decision_lock` (примитивы, фичей не тронуты); корректность побайтовой пересылки (живёт в модуле repeater); `mode_to_str(REPEATER)` и `set_mode_repeater_success` — **уже покрыто** (проверено).

### Модуль 3 — info/API/e2e (`info_handlers.c` блок `repeater`, `openapi.yaml`, `api_tests/13_test_ports.py`)
Единственное место, где живая интеграция (UART→repeater→peer) реально проверяется.

- **Contract-тест добавить:**
  - **C-1 · форма объекта `/info.repeater`** (GET /info, блок `info_handlers.c:341–353`). Объект присутствует; ключи `{active, uptime_s, bytes_1to2, bytes_2to1, dropped_1, dropped_2}` все есть; `active` — bool, остальные — int ≥ 0. *Ловит:* пропавшее/переименованное поле, неверный тип (счётчик строкой), дрейф схемы между прошивкой и `openapi.yaml`. **Единственное покрытие нового блока `info_handlers.c`;** дёшево, без serial-обвязки. Добавить функцией в `13_test_ports.py`.
- **E2E-тесты добавить** (≤10 на проект; каждый — отдельный user journey):
  - **E2E-1 · живая пересылка обоих направлений.** *User journey:* «Оператор включает repeater на обоих портах, и шлюз прозрачно мостит трафик в обе стороны». Оба порта → `repeater`; raw-сокеты к chardev UART1 (127.0.0.1:5561) и UART2 (:5562); записать N байт в 5561, прочитать из 5562; GET /info → `active==true`, `bytes_1to2 >= N`; зеркально для `bytes_2to1`. *Ловит:* пересылку «не туда»/«никуда», счётчик в неверном направлении, `active` не отражает оба порта, режим возвращает 200 но serial-путь не открыт — **класс дефектов, который замоканный unit структурно поймать не может.**
  - **E2E-2 · негатив: только один порт в repeater.** *User journey:* «Оператор включил repeater лишь на одном порту — шлюз не должен слать в мёртвого пира и обязан показать линк неактивным». Порт 1 → `repeater`, порт 2 → `disabled`; GET /info → `active==false`; записать байты в 5561 → `dropped_1` вырос, `bytes_1to2` — нет. *Ловит:* не-NULL дескриптор пира при одном порту, ложный `active`, зачёт байтов как переданных вместо dropped.
  - **Замечание по бюджету:** если проект у глобального потолка ≤10 E2E — **слить E2E-1 и E2E-2 в один параметризованный** тест «жизненный цикл repeater-линка» (рекомендуется).
- **НЕ тестировать:** приём/отклонение enum режимов (`13_test_ports.py` уже покрывает — проверено); арифметику пересылки/drop/uptime на e2e (покрыто на unit — поэтому e2e утверждает только `>=`/наличие/направление); `openapi.yaml` как самостоятельный артефакт (документация); `cJSON_*` и `if (repeater_json)` OOM-guard (третья сторона / недостижимая ветка).

### Модуль 4 — фронтенд (`main/frontend/src/views/Repeater.vue` + тест)
Логика тоггла покрыта хорошо; не покрыты чистые хелперы и page-local error-path.

- **Integration-тест добавить (без рефактора, наивысший ROI на фронте):**
  - **F-2 · error-path тоггла `Repeater.vue`.** Оба порта `disabled`, клик «включить», `api()` реджектится → `showAlert('connection_error')` вызван, кнопка откатилась в `off`. *Ловит:* регрессию проводки `onError` (неверный ключ алерта / проглоченная ошибка) или потерю отката. Текущие 5 тестов держат `api()` зарезолвленным, поэтому проводка `Repeater.vue:24` **не исполняется ни разу.**
- **Unit-тест добавить (чистые хелперы; зависит от рефактора R4):**
  - **F-1 · `avgBytesPerSec` / `formatUptime` / `groupBytes` / `lineParams`.** Граничные ветки: `avgBytesPerSec` uptime=0 → 0 (защита от деления на ноль) и округление до 0.1; `formatUptime` отрицательное → `00:00:00`, >99 ч не обрезается; `groupBytes` дробное/отрицательное; `lineParams` parity even/odd/`undefined→'—'`. *Ловит:* утечку NaN/Infinity в UI, off-by-one и clamp в форматировании времени, неверную букву чётности. ROI: высокий для `avgBytesPerSec`/`lineParams`, умеренный для остальных.
- **НЕ тестировать:** i18n-метки (5 локалей — тавтологический снапшот словаря об самого себя); маршрут `/repeater` и иконка/пункт sidebar (framework wiring); типы `RepeaterStats`/`PortMode`/`Info.repeater?` (compile-time DTO); guard двойного клика и механика revert/`fetchInfo('low')` `useOptimisticToggle` — **уже покрыто на нижнем слое** (`useOptimisticToggle.test.ts`); тривиальные геттеры `forwardBytes`/`uptimeS` и `?? 0` (покрыто REP-I-005).

---

## 3. Сквозные аспекты

- **Contract-тесты между сервисами:** один — C-1 (форма `/info.repeater` против `openapi.yaml`). Между микросервисами контрактов нет (монолитная прошивка + один фронтенд).
- **Property-based:** не оправдано для этой фичи; пространство входов хелперов узкое, проще точечные граничные кейсы.
- **Дымовые нагрузочные:** не требуются — пропускную способность повторителя ограничивает UART, а не код; e2e проверяет лишь корректность направления, не throughput.
- **Test-data factories:** новый хелпер **`_UartByteProbe`** для E2E (raw-сокет к chardev: `send(bytes)` + `recv(n, timeout)` с тайм-аутом) — по образцу `_UartEchoThread` из `25_test_transparent_tcp_e2e.py`, но без эха (RTU-slave не подходит — он бы авто-отвечал и засорял поток). Плюс `_read_repeater_stats(api)` = GET /info → `["repeater"]` для читаемости. Порты 5561/5562 уже есть в `conftest.py`.

## 4. Обнаруженные антипаттерны

- **Скрытые/недетерминируемые при unit-тестах:** реальная гонка `repeater_rx_handler` ↔ `repeater_deinit_port` недостижима в host-харнессе (FreeRTOS-моки однопоточны, `xSemaphoreTake` всегда `pdPASS`). Вывод: не писать многопоточный тест (флак), а проверять **инвариант порядка** через `call_seq` (U-R4). Инвариант — самое рискованное свойство модуля.
- **Мок-ограничение, мешающее unit-тесту:** `mocks/bridge.c::bridge_read_serial_config` всегда зануляет конфиг, поэтому снимок `serial_cfg_at_init` и текущее чтение всегда совпадают → под-ветка «серийные параметры изменились» в `check_settings_changed(REPEATER)` недостижима (см. R3).
- **Хрупкая ассерция в существующем тесте:** `Repeater.integration.test.ts:311-312` матчит по схлопнутому тексту страницы (`"Dropped bytes3"`) — сломается при вставке любого элемента между `<dt>`/`<dd>`. Низкая важность; лучше скоупить запрос на конкретный `.rep-port-stats dd`.
- **Порядко-зависимые / нестабильные тесты:** не обнаружено — существующие 10+44+5 тестов чистые (правильный `unmount()`, нет hard-sleep, нет тавтологий). Единственный риск порядка — новые тесты на счётчики мьютекса/`call_seq` (`s_lock` статичен и не сбрасывается между тестами) → решается R1/R2.

## 5. Необходимые рефакторинги перед написанием тестов

- **R1 — сброс `s_lock` для тестов.** `repeater_reset_for_test()` не обнуляет статический `s_lock`, а `setUp` не сбрасывает semaphore-мок → тесты на счётчик создания мьютекса станут порядко-зависимыми. Либо добавить `s_lock = NULL;` в `repeater_reset_for_test()`, либо в новом тесте звать `mock_freertos_semaphore_reset()` и проверять только относительное поведение. *Блокирует:* **U-R1**.
- **R2 — `deinit_call_seq` в serial-моке.** В `unittests/repeater/mocks/serial.c::serial_deinit` записывать `call_sequence_get_call_id()`, чтобы тест мог утверждать `deinit_call_seq > xSemaphoreGive_call_seq`. *Блокирует:* точную версию **U-R4** (слабая версия — без R2).
- **R3 — инъекция конфига в bridge-мок.** Добавить `mock_bridge_set_serial_config(index, cfg)` в `unittests/port_manager/mocks/bridge.c`, чтобы init и check видели разный конфиг. *Блокирует:* под-ветку смены серийных параметров в **U-P2** (под-ветка смены режима — не блокируется). Это правка тест-двойника, не продакшен-кода.
- **R4 — вынос чистых хелперов из SFC.** Перенести `avgBytesPerSec`/`groupBytes`/`formatUptime`/`lineParams` (`Repeater.vue:55-81`) в `src/views/repeaterFormat.ts` (как `utils/modbusUtils.ts` + `.test.ts`). Механический перенос без изменения поведения. *Блокирует:* **F-1**. Если команда откажется — хотя бы добавить ассерцию `avgBytesPerSec` (uptime=0 → 0) в integration-тест.

> Продакшен-код фичи уже тестопригоден: движок изолирован в `repeater.c` с чистым аксессором `repeater_get_stats()`. Все рефакторинги выше — это правки тест-обвязки (R1–R3) и косметический вынос (R4), а не изменение логики.
