# Отчёт по тест-стратегии — wb-mge — 2026-05-27

## 1. Исполнительное резюме

Critical / High
1. tcp_server_deinit can hang forever, blocking port_manager  tcp_server.c:386-389
Receiver tasks only exit when recv() returns ≤0. Deinit closes the listen socket but never shutdown()s active client sockets. Any idle client with TCP keep-alive keeps its recv() blocked indefinitely, so tcp_server_deinit busy-waits forever on active_connections > 0. Because port_manager_set_mode / apply_settings hold pm_ctx[index].init_mutex with portMAX_DELAY across deinit, this hangs the entire port-mode subsystem. Need per-client shutdown or a forced-teardown list.

2. Modbus TCP pending_* state racy on disconnect  modbus_tcp.c:332-349, modbus_tcp.c:202, modbus_tcp.c:511
on_tcp_conn_close (per-connection receiver task), process_data_from_serial (UART cb context), and modbus_tcp_server_task all read/write ctx->pending_tid, pending_slave_id, pending_client_sock with no mutex. The comment claims fd-reuse aliasing is prevented, but the writes themselves are unsynchronised. Use _Atomic or a small mutex.

3. port_manager lazy mutex creation has TOCTOU  port_manager.c:45-53
pm_lock checks init_mutex == NULL and creates the mutex without protection. Two concurrent first-callers (HTTP handler + settings task) both pass the null check, both create, both leak/race. Create all BRIDGES_COUNT mutexes synchronously in port_manager_init.

4. Settings apply is not actually atomic on NVS write failure  settings_manager.c:537-552
The "validate-then-save" claim only covers validator rejection. setting_items_save* return values inside save_setting_from_json / save_group_settings / process_rs485_settings are logged and discarded, then the response is success:true. A partial-write window is fully observable to the client.
**Исправлено**: `save_setting_from_json`, `save_group_settings` и `process_rs485_settings` теперь возвращают ошибку наверх; при любом сбое записи в NVS вся цепочка прерывается и клиент получает `success:false` с сообщением об ошибке.

5. validate_password now rejects empty for STA Wi-Fi password (regression)  setting_validators.c:344-346
STA passwords must be empty for WIFI_AUTH_OPEN_STR networks. The empty-password tightening (good for login) now blocks switching STA to an open AP through the API. Split into validate_login_password vs validate_wifi_password or branch on auth mode.

6. Listen socket re-create can silently kill the acceptor  tcp_server.c:217-226
On non-EMFILE/ENFILE/ENOMEM accept errors the listen socket is recreated; if recreate fails the acceptor task exits with no upper-layer notification. Server is dead but tcp_server_connected() still reports OK (active receivers exist). No escalation path back to port_manager.

7. Cache Modbus server falls back to raw-buffer processing when conn table is full  cache_modbus_server.c:412-417
With >CACHE_MB_MAX_CONNS (8) clients, reasm_get() returns NULL and the code processes the raw recv buffer as if it were a single complete frame, re-introducing exactly the partial/coalesced-read bug the reassembler exists to fix. Reject the connection instead of bypassing framing.

8. Sniffer response timer races with arbitration-only branch  sniffer.c:485 ✅ Пофикшено
The early goto exit_critical for the FM-arbitration-in-RES_WAIT case runs before should_stop_timer = true, so the response timer fires later with req_len ≥ 2 still true and resp_timer_cb emits a spurious duplicate timeout packet for an already-handled request.

Medium
9. Modbus TCP cache lookup is O(N²) on hot path  cache_modbus_server.c:124-141, cache_multimaster.c:426-444
FC03 with count=125 → 125 linear scans of CACHE_MAX_ENTRIES=4096 under the mutex (~500K compares per request, 125 mutex round-trips). FC01 count=2000 → 8M compares. Blocks aging task and other lookups. Add a batch lookup_range API.

10. Cache aging task deletion via vTaskDelete while holding mutex  cache_multimaster.c:241-249
Relies on FreeRTOS removing a blocked task from a semaphore wait list cleanly. Works on current ESP-IDF but fragile. Prefer a stop-flag + join pattern.

11. serial_set_tx_disabled touches serial_desc_t* without pm_lock  port_manager.c:319-329
get_port_serial_desc() pointer can be freed by a concurrent mode change before gpio_reset_pin/uart_set_pin dereference it. Wrap the whole get-and-use under pm_lock(port_index).

12. Stream splitter Level-3 CRC scan is O(N²) and unbounded  stream_splitter.c:194-200 ✅ Пофикшено
For a 256-byte merged buffer, worst case is thousands of CRC16 ops per sniffer_process() inside the UART event task — blocks subsequent UART reads. Cap try_len at Modbus max (~256) or break after K misses.

13. port_manager_set_mode / apply_settings don't roll back NVS on port_init_mode failure  port_manager.c:343-353
NVS is updated before init runs; if init fails the device boots into the broken mode next time.

14. Receiver-task active_connections decrement is non-atomic, but deinit polls it  tcp_server.c:181-182
Lost decrement on dual-core → deinit deadlock. Use atomic counter or counting semaphore.

15. Number-validation truncation in settings  settings_manager.c:111-117
(int)item->valuedouble is UB on out-of-range doubles. Range-check the double against INT_MIN/INT_MAX first.
**Исправлено**: перед приведением `double` к `int` добавлена проверка `valuedouble >= (double)INT_MIN && valuedouble <= (double)INT_MAX`; значения вне диапазона отклоняются ещё на фазе валидации (Phase 1) до любой записи в NVS.

16. wifi_perm_disable save failure leaves in-memory flag inconsistent  settings_manager.c:535-542
Rest of request silently skips WiFi group even though NVS still says Wi-Fi is enabled.
**Исправлено**: при неудаче записи `wifi_perm_disable` в NVS запрос немедленно прерывается с `success:false`; in-memory флаг меняется только после подтверждённой записи.

17. Sniffer multi-master misclassification in RES_WAIT  sniffer.c:514 ✅ Пофикшено
A second master starting a new request during arbitration is paired as a response. Add a classify_direction check before pairing.

18. WS reconnect timer leaks across unmount  Sniffer.vue:73-81 ✅ Пофикшено
setTimeout(connectWs, 2000) handle not captured, not cleared in stopCapture / onUnmounted. Orphan WebSocket fires on a destroyed component.

19. toggleCaching / resetMap have no in-flight guard  RegisterMap.vue:104-135 ✅ Пофикшено
Double-clicks can interleave disable/enable POSTs across ports. Add isMutating ref (like settingsSaveStatus === 'saving').

20. watch(cacheEnabled, ..., { immediate: true }) can double-schedule statsInterval  RegisterMap.vue:300-316 ✅ Пофикшено
True → undefined → true transition starts a second setInterval without clearing the first. Clear at the top of the true branch.

Low / fragility notes
Sniffer sniffer_disable() does not clear sniff_handler or join in-flight callbacks (sniffer.c:829-846) — safe today because callbacks remain static, but fragile. ✅ Захарднено (req_len=0 + enabled guard in resp_timer_cb)
Sniffer 1000-packet cap silently stops capture (Sniffer.vue:61-64) — show a banner.
parsePacket(msg: any, …) (snifferUtils.ts:135) — no presence/type guards on msg.slave_id, msg.function, etc. Malformed frame throws & is swallowed; lastTimestampUs stale afterwards.
PacketDecoder.findDeepest has no recursion cap (PacketDecoder.vue:109-122).
SNIFFER_JSON_BUF_SIZE snprintf return value discarded (sniffer.c:619-622) — silent truncation if SNIFFER_MAX_PACKET_LEN is ever raised.
mbtcp_frame_total_len callers depend on sizeof(mb_tcp_header_t) == 7 (modbus_tcp.c:285) — prefer offsetof or a literal >= 6.
Logout doesn't Set-Cookie: …; Max-Age=0 (auth.c:285-305) — server session cleared but stale cookie sits in browser.
OTA accepts missing Content-Type (ota_handler.c:71-78).
Cache server start/stop TOCTOU between read of cache_modbus_server_get_port() and the enable decision (settings_manager.c:576-620). ✅ **Исправлено**: результат `cache_modbus_server_get_port()` сохраняется в локальную переменную `running_port` один раз; вся логика принятия решений (is running? / stop) использует этот снимок.
voltage_monitor_task reads shared fields after releasing mutex (voltage_monitor.c:131-139).
MAX_URI_HANDLERS = 30 not enforced (http_server.c:20) — silent drop if exceeded.
i18n gaps in Sniffer.vue, PacketDecoder.vue, SerialPorts.vue, Dashboard.vue, System.vue — many hardcoded user-visible strings ("Slave ID", "Function code", "Master → Slave", RS-485 · Port 1/2, PSRAM, etc.). Several keys defined in messages.ts but unused.

Test-coverage gaps worth filling
No test for cache_modbus_server's conn-table-full fallback path (Critical #7 area).
No test for concurrent enable/disable of cache against in-flight HTTP handlers or lookups (race #2/#10 area).
No test that cache_modbus_server_deinit cleans up reassembly state for active sockets.
No stress test demonstrating the O(N²) cache hot path (#9) — would catch any future hold-time regression.
No test for tcp_server_deinit with active idle clients (Critical #1).
No test for port_manager_set_mode rollback on init failure (#13).
~~No test that NVS write failure inside settings_process_request_json results in a success:false response (Critical #4).~~ ✅ Покрыто тестами в `unittests/settings_manager/settings_manager_test.c`

---

## 2. Рекомендации по модулям

### 2.1 `main/bridge/tcp_server.c`

**Кандидаты на извлечение в чистые функции:**
- [`tcp_server_connected()`](main/bridge/tcp_server.c:352) — уже тестируется, новых тестов не нужно

**Unit-тесты добавить:**

| ID | Цель | Сценарий | Дефект |
|----|------|----------|--------|
| U1 | [`tcp_server_run_receiver_for_test()`](main/bridge/tcp_server.c:405) ошибочный путь | `recv()` возвращает -1 → `close_handler` всё равно должен вызываться | `close_handler` пропускается при сетевой ошибке → утечка ресурсов Modbus-reassembly |
| U2 | [`tcp_server_run_receiver_for_test()`](main/bridge/tcp_server.c:405) порядок shutdown | После `recv=0`: `shutdown(SHUT_RDWR)` до `close()` | Регрессия удаления `shutdown()` |
| U3 | [`tcp_server_run_receiver_for_test()`](main/bridge/tcp_server.c:405) `last_client_sock` | `recv()` вернул данные → `desc.last_client_sock == client_sock` | Неверная маршрутизация ответа |
| U4 | [`tcp_server_send()`](main/bridge/tcp_server.c:327) | happy path / partial send / `send()=-1` / NULL desc / `sock<0` | Молчаливая потеря данных при partial send |
| U5 | [`tcp_server_deinit()`](main/bridge/tcp_server.c:361) NULL-guard | NULL desc → `ESP_ERR_INVALID_ARG` | Краш при deinit частично-инициализированного дескриптора |
| **U6** ⚠️ | **[`tcp_server_deinit()`](main/bridge/tcp_server.c:390) зависание — Bug #1** | `active_connections=1`, deinit: проверить что `shutdown()` NOT вызван на клиентских сокетах → deinit зависает | **Bug #1: deinit не вызывает `shutdown()` на клиентских сокетах** |
| **U7** ⚠️ | **[`tcp_server_task()`](main/bridge/tcp_server.c:221) тихий выход — Bug #6** | `mock_accept_errno=ECONNRESET`, пересоздание сокета не удалось → acceptor тихо завершается без уведомления | **Bug #6: верхний уровень не узнаёт о смерти acceptor** |
| U8 | [`tcp_server_task()`](main/bridge/tcp_server.c:188) успешное пересоздание | `ECONNRESET` + успешный пересоздание → `mock_close_call_count==1` | Двойное закрытие / двойное пересоздание |
| U9 | [`tcp_server_task()`](main/bridge/tcp_server.c:247) сбой `xTaskCreate` | 2-й `xTaskCreate` падает → клиентский сокет закрыт, `active_connections==0` | Утечка сокета при невозможности создать задачу |
| U10 | [`tcp_server_init()`](main/bridge/tcp_server.c:270) сбой `listen()` | `listen()` падает → утечка fd | Утечка дескриптора сокета |

**Integration-тесты добавить:**

| ID | Сценарий | Дефект |
|----|----------|--------|
| I1 | 2 вызова `tcp_server_run_receiver_for_test()` → `active_connections==0` | Документирование неатомарного декремента (**Bug #14**) |
| I2 | init → acceptor (EXIT_REQ до accept) → deinit с `active_connections==0` | Утечка памяти в нормальном teardown |
| I3 | init → accept клиента → receiver → deinit | End-to-end lifecycle без зависания |

**E2E-тесты добавить:**

| Journey | Файл | Дефект |
|---------|------|--------|
| Клиент подключён во время рекonfigурации порта | новый `api_tests/` QEMU-тест | **Bug #1** в production |
| Насыщение accept (много TCP-соединений) | новый `api_tests/` QEMU-тест | **Bug #6** EMFILE + пересоздание |

**НЕ тестировать:** [`check_task_exit_req()`](main/bridge/tcp_server.c:25), [`create_listen_socket()`](main/bridge/tcp_server.c:35), [`accept_connection()`](main/bridge/tcp_server.c:103), [`tcp_server_connected()`](main/bridge/tcp_server.c:352) (уже покрыт), `setsockopt`, логи.

---

### 2.2 `main/bridge/modbus_tcp.c`

**Кандидаты на извлечение:**
- [`calc_response_timeout_ticks()`](main/bridge/modbus_tcp.c:555) — pure arithmetic, тест монотонности уже частично есть
- Предикат в [`on_tcp_conn_close()`](main/bridge/modbus_tcp.c:332) — false-ветвь **не покрыта**

**Unit-тесты добавить:**

| ID | Цель | Сценарий | Дефект |
|----|------|----------|--------|
| **MBTCP-U-025** ⚠️ | [`on_tcp_conn_close()`](main/bridge/modbus_tcp.c:332) matching-sock | sock=7 → все три `pending_*` сброшены | TID-aliasing: stale TID от отключённого клиента |
| **MBTCP-U-026** ⚠️ | [`on_tcp_conn_close()`](main/bridge/modbus_tcp.c:332) non-matching-sock | sock=99 → поля НЕ изменяются | Over-eager clear: ответ живого клиента теряется |
| **MBTCP-U-027** ⚠️ | [`on_tcp_conn_close()`](main/bridge/modbus_tcp.c:332) sock==-1 | Нет crash, поля неизменны | Нечаянное совпадение sentinel |
| MBTCP-U-028 | [`make_rtu_request_from_tcp()`](main/bridge/modbus_tcp.c:430) | TID=`0xBEEF`/`0x0000`/`0xFFFF` → `pending_tid` содержит правильное значение | Endian-flip в `modbus_swap16` |
| MBTCP-U-029 | [`calc_response_timeout_ticks()`](main/bridge/modbus_tcp.c:555) | 9600 > 19200 > 115200 baud → монотонно уменьшающийся timeout | Инверсия отношения |
| MBTCP-U-030 | [`on_tcp_conn_close()`](main/bridge/modbus_tcp.c:332) reasm + pending | sock=42 с частичным фреймом + `pending_client_sock=42` → слот освобождён И поля сброшены | Partial cleanup |

**Integration-тесты добавить:**

| ID | Сценарий | Дефект |
|----|----------|--------|
| MBTCP-I-001 | `process_data_from_serial` → `tcp_server_send` с правильным TID | Полный путь ответа, fd-aliasing |
| MBTCP-I-002 | Disconnect до RTU-ответа → поля очищены → ответ не идёт на sock 5 | **Bug #2** sequential approximation |
| MBTCP-I-003 | Неродственный close sock=99 не трогает pending sock=5 | Over-clearing |

**E2E-тесты добавить:**

| Journey | Дефект |
|---------|--------|
| FC03 request → ответ с matching TID | full-stack TID |
| Клиент отключается до ответа RTU; новый клиент получает только свой ответ | **Bug #2** |
| Два клиента одновременно — ответы без перекрёстного заражения | Multiplexing |

**НЕ тестировать:** `modbus_tcp_init_port`, `modbus_tcp_deinit_port`, `check_task_exit_req`, `find_ctx_*`, `wait_tcp_connection`, `fetch_tcp_request`, `send_rtu_request`, `wait_rtu_send_receive`, `modbus_tcp_server_task`.

---

### 2.3 `main/bridge/port_manager.c`

**Кандидаты на извлечение:**
- [`str_to_pm_mode()`](main/bridge/port_manager.c:77) — static, покрывается через HTTP-handler mock

**Unit-тесты добавить:**

| ID | Цель | Сценарий | Дефект |
|----|------|----------|--------|
| **U1** ⚠️ | [`pm_lock()`](main/bridge/port_manager.c:45) TOCTOU | Двойное создание mutex: `xSemaphoreCreateMutex` вызван дважды | **Bug #3: утечка mutex при конкурентной инициализации** |
| **U2** ⚠️ | [`port_manager_set_mode()`](main/bridge/port_manager.c:332) без rollback | `port_init_mode` падает → NVS уже записан с новым режимом | **Bug #13: устройство загружается в сломанный режим** |
| **U3** ⚠️ | [`port_manager_set_tx_disabled()`](main/bridge/port_manager.c:319) без lock | `serial_set_tx_disabled` вызывается ДО `pm_lock` | **Bug #11: dangling pointer на serial_desc_t** |
| U4 | [`port_manager_check_settings_changed()`](main/bridge/port_manager.c:373) | mode changed / config changed / unchanged — все 6 ветвей | Ненужные рестарты портов |
| U5 | [`port_manager_init()`](main/bridge/port_manager.c:276) | `cache_server_enabled=false` → `cache_modbus_server_init` NOT called | Пропущенная инициализация |
| U6 | [`port_deinit_mode()`](main/bridge/port_manager.c:212) | Два порта CACHE_BUS, deinit в обоих порядках | Off-by-one в `cache_bus_count` |

**Integration-тесты добавить:**

| ID | Сценарий | Дефект |
|----|----------|--------|
| I1 | `set_mode` → `apply_settings` round-trip | Взаимодействие NVS-write и NVS-read; **Bug #13** |
| I2 | `port_manager_init()` полная загрузка | boot sequence regression |

**E2E-тесты добавить:**

| Journey | Дефект |
|---------|--------|
| POST `/ports/1/mode` → GET `/info` показывает новый режим | HTTP → PM wiring |
| Смена режима пережила ребут | **Bug #13** в нормальном пути |
| Неверный режим отклонён с 4xx | `str_to_pm_mode` guard |

**НЕ тестировать:** `port_manager_mode_to_str` (покрыт), `port_manager_get_mode` (покрыт), `pm_unlock` (trivial), `port1/2_set_mode_handler` (pass-through), `port_manager_register_handlers` (wiring), `port_manager_reset_for_test` (test infra).

---

### 2.4 `main/settings_manager.c` + `main/setting_validators.c`

> ⚠️ **Критично: `settings_manager.c` имеет ~0% unit-покрытия** несмотря на наибольшую cyclomatic complexity в проекте.

**Кандидаты на извлечение:**
- [`validate_setting_from_json()`](main/settings_manager.c:181) — содержит UB-cast (**Bug #15**)
- [`save_setting_from_json()`](main/settings_manager.c:105) — возвращаемые значения игнорируются (**Bug #4**)
- [`validate_password()`](main/setting_validators.c:338) → разделить на `validate_login_password` и `validate_wifi_password` (**Bug #5**)

**Unit-тесты добавить (новый сюит `unittests/settings_manager/`):**

| ID | Цель | Сценарий | Дефект |
|----|------|----------|--------|
| **SM-U-001** ⚠️ | [`settings_process_request_json()`](main/settings_manager.c:537) частичная запись | NVS-write #2 падает → ответ должен содержать `success:false` | **Bug #4: partial-write observability** |
| **SM-U-002** ⚠️ | [`validate_setting_from_json()`](main/settings_manager.c:206) UB-cast | double > INT_MAX, < INT_MIN, NaN, Infinity → reject | **Bug #15: UB при `(int)valuedouble`** |
| **SM-U-003** ⚠️ | [`validate_wifi_password()`](main/setting_validators.c:344) | `""` → valid (open network) | **Bug #5: регрессия STA open-AP** |
| SM-U-003b | `validate_login_password("")` | `""` → invalid | **Bug #5** (post-fix) |
| **SM-U-004** ⚠️ | [`settings_process_request_json()`](main/settings_manager.c:537) `wifi_perm_disable` NVS-fail | save fails → in-memory flag NOT set; wifi group всё равно обрабатывается | **Bug #16: in-memory/NVS inconsistency** |
| SM-U-005 | [`settings_build_response_json()`](main/settings_manager.c:344) | `wifi_perm_disable=true` → нет wifi-объекта в ответе | Неверный вывод wifi при отключённом WiFi |
| SM-U-006 | `validate_group_settings()` | NULL group → `true`; JSON array → `false` | Wrong-type substitution |

**Integration-тесты добавить:**

| ID | Сценарий | Дефект |
|----|----------|--------|
| SM-I-001 | `settings_process_request_json` + RAM storage, все группы | full pipeline, mapping key typos |
| SM-I-002 | POST `{"wifi":{"sta_pass":"","sta_auth":"open"}}` после R3 | **Bug #5** wiring |

**E2E-тесты добавить:**

| Journey | Дефект |
|---------|--------|
| POST `{"wifi":{"mode":"sta","sta_auth":"open","sta_pass":""}}` → 200 success | **Bug #5** |
| Частичный NVS-сбой → ответ НЕ `success:true` | **Bug #4** |
| `wifi_perm_disable:true` при полном NVS → failure; WiFi остаётся рабочим | **Bug #16** |

**НЕ тестировать:** `settings_get_handler`/`settings_post_handler` (wiring), статические маппинг-таблицы, `validate_bool` (покрыт), все 9 покрытых валидаторов в `setting_validators_test.c`.

---

### 2.5 `main/bridge/cache_modbus_server.c` + `main/bridge/cache_multimaster.c`

**Кандидаты на извлечение:**
- [`frame_total_len()`](main/bridge/cache_modbus_server.c:402) — static, нужен shim
- [`find_or_alloc_entry()`](main/bridge/cache_multimaster.c:97) — коллизия типов не покрыта

**Unit-тесты добавить:**

| ID | Цель | Сценарий | Дефект |
|----|------|----------|--------|
| **CMS-U-NEW-1** ⚠️ | [`process_data_from_tcp()`](main/bridge/cache_modbus_server.c:413) Bug #7 | 8 слотов заняты, 9-й клиент + partial frame → reject, НЕ `process_one_frame(raw)` | **Bug #7: partial-frame dispatch при полной таблице** |
| CMS-U-NEW-2 | [`frame_total_len()`](main/bridge/cache_modbus_server.c:402) | `buf[4..5]={0,6}→12`, `{1,0}→262`, `{0,1}→7` | Неверная граница ADU |
| CM-U-NEW-1 | [`cache_multimaster_on_response()`](main/bridge/cache_multimaster.c:305) сброс age | Установить age=500 → on_response → lookup с timeout=1 → FOUND | age не сбрасывается при обновлении |
| CM-U-NEW-2 | FC01 byte_count clamping | `byte_count=0` → count clamped to 0, no crash | Integer arithmetic в FC01/FC02 |
| CM-U-NEW-3 | Bug #9 regression guard | `build_register_response` с count=125 → `mock_lookup_call_count==125` (не 15625) | O(N²) regression |
| **CM-U-NEW-4** ⚠️ | [`cache_multimaster_disable()`](main/bridge/cache_multimaster.c:230) Bug #10 | mutex balanced (give==take) после disable(); `s_cache_enabled=false` ДО взятия mutex | **Bug #10: vTaskDelete с удержанием mutex** |
| CM-U-NEW-5 | enable → disable → enable cycle | Lookup работает только для данных 3-го цикла | double-free/dangling pointer |

**Integration-тесты добавить:**

| ID | Сценарий | Дефект |
|----|----------|--------|
| CMS-I-001 | split-frame (6+6 bytes) + реальный `cache_multimaster` | mis-wiring между reassembly и lookup |
| CM-I-001 | on_response → tick age → stale → on_response → FOUND | mutex защищает запись И тикание |

**E2E-тесты добавить:**

| Journey | Дефект |
|---------|--------|
| 9 одновременных TCP-клиентов → 9-й получает Modbus exception | **Bug #7** |
| FC03 count=125 → латентность < 200 ms | **Bug #9** |
| Entry → stale → fresh response → повторный успешный запрос | Lifecycle |

**НЕ тестировать:** `cache_modbus_server_get_port`, `cache_modbus_server_init/deinit` (wiring), `cache_multimaster_is_enabled` (trivial), `cache_multimaster_register_handlers` (wiring), HTTP-handler функции (покрыты).

---

### 2.6 `main/bridge/sniffer.c`

**Кандидаты на извлечение (уже протестированы):**
`crc_check`, `classify_direction`, `bytes_to_hex`, `format_timeout_json`, `format_packet_json`, `strip_arbitration`, `fm_is_slave_subcmd` — все полностью покрыты.

**Unit-тесты добавить:**

| ID | Цель | Сценарий | Дефект |
|----|------|----------|--------|
| **SN-U-NEW-1** ✅ | `resp_timer_cb` — Bug #8 | FC03 req → арбитраж-только frame → `goto exit_critical` БЕЗ `should_stop_timer=true` → timer fires → очередь должна быть ПУСТОЙ после срабатывания | **Bug #8: spurious timeout packet** → покрыт TC-19 |
| **SN-U-NEW-2** ✅ | [`sniffer_process()`](main/bridge/sniffer.c:514) Bug #17 | Master-1 FC03 → Master-2 FC03 в RES_WAIT → pkt[1].is_master==true, НЕ false | **Bug #17: second master misclassified as slave** → TC-18 |
| SN-U-NEW-3 ✅ | [`sniffer_disable()`](main/bridge/sniffer.c:829) | disable → timer stopped → timer callback → очередь пустая | Fragility: post-disable delivery → TC-19 |
| SN-U-NEW-4 | [`classify_direction()`](main/bridge/sniffer.c:242) FC03 len=8 data[2]=3 | → DIRECTION_REQUEST (не UNKNOWN) | Precondition для Bug #17 fix |
| SN-U-NEW-5 | [`classify_direction()`](main/bridge/sniffer.c:242) FC10 req vs resp | len=8→RESPONSE; len=11 data[6]=2→REQUEST | Off-by-one в Write-Multiple-Registers |

**Integration-тесты добавить:**

| ID | Сценарий | Дефект |
|----|----------|--------|
| SN-I-001 | FC03 → арбитраж → следующий нормальный FC03 → ровно 2+2 пакета, без timeout | **Bug #8** full sequence |
| SN-I-002 | disable → enable → свежий FC03 → ровно 1 пара без stale данных | req_buf утечка при disable/enable |
| SN-I-003 | Multi-master: master-1 → master-2 → slave-2 response | **Bug #17** full sequence |

**E2E-тесты добавить:**

| Journey | Дефект |
|---------|--------|
| WS sniffer: FM arbitration exchange → нет spurious timeout в WS-стриме | **Bug #8** |
| WS sniffer: multi-master bus → all is_master correct | **Bug #17** |
| Disable sniffer → нет новых пакетов в WS | Fragility |

**НЕ тестировать:** `port_index_to_name/port_name_to_index`, `sniffer_ws_task`, `sniffer_register_handlers`, `sniffer_status_handler`, `sniffer_attach/detach`, `sniffer_receive_cb_0/1`, `try_enqueue`.

---

### 2.7 Frontend (`main/frontend/src/`)

**Кандидаты на извлечение:**
- [`findDeepest()`](main/frontend/src/components/PacketDecoder.vue:110) — встроенная рекурсивная функция без ограничения глубины; требует выноса в `packetDecoderUtils.ts`

**Unit-тесты добавить:**

| ID | Цель | Сценарий | Дефект |
|----|------|----------|--------|
| **FE-U-001** ⚠️ | [`parsePacket()`](main/frontend/src/utils/snifferUtils.ts:135) missing fields (timeout) | `slave_id=undefined`, `function=undefined`, `timestamp_us=undefined` → no throw, `prevTimestampUs` не испорчен | Stale `lastTimestampUs` после throw |
| **FE-U-002** ⚠️ | [`parsePacket()`](main/frontend/src/utils/snifferUtils.ts:135) missing fields (packet) | `slave_id=undefined`, `raw=undefined` → no throw | TypeError swallowed |
| **FE-U-003** ⚠️ | `findDeepest()` (после R2) | depth=100 → no stack overflow; circular ref → return null | **Stack overflow на глубоко вложенных фреймах** |

**Integration-тесты добавить:**

| ID | Файл | Сценарий | Дефект |
|----|------|----------|--------|
| **FE-I-001** ✅ | [`Sniffer.integration.test.ts`](main/frontend/src/views/Sniffer.integration.test.ts) | WS close → reconnect scheduled → unmount BEFORE timeout → нет новых WebSocket после unmount | **Bug #18: orphan WS после unmount** |
| FE-I-001b ✅ | то же | `stopCapture()` → no reconnect setTimeout | Bug #18 intentional stop |
| **FE-I-002** ✅ | [`RegisterMap.integration.test.ts`](main/frontend/src/views/RegisterMap.integration.test.ts) | Double-click toggleCaching → api вызван ровно 1 раз | **Bug #19: no in-flight guard** |
| FE-I-002b ✅ | то же | Double resetMap → api ровно 1 раз | Bug #19 |
| **FE-I-003** ✅ | [`RegisterMap.integration.test.ts`](main/frontend/src/views/RegisterMap.integration.test.ts) | `cacheEnabled`: undefined→true→undefined→true → `setInterval` вызван ровно 1 раз | **Bug #20: double statsInterval** |
| FE-I-003b | то же | unmount с активным interval → `clearInterval` вызван | Bug #20 утечка |

**E2E-тесты добавить:**

| Journey | Дефект |
|---------|--------|
| Sniffer: start → packet → unmount → нет Vue-warnings через 3с | **Bug #18** |
| RegisterMap: double-click Enable → UI stable, нет flickering | **Bug #19** |
| RegisterMap: info-poll blip → stats panel stable, 1 interval | **Bug #20** |

**НЕ тестировать:** FC_NAMES/lookup tables, `getWsUrl`, `sendPortStart/Stop`, CSS style helpers, computed properties (covered by utils tests), `openUrl`, `toggleDevice/Group`, `formatAgeUs/Memory` (покрыты), все modbusDecoder.ts.

---

## 3. Сквозные аспекты

### Property-based тесты
- [`calc_response_timeout_ticks()`](main/bridge/modbus_tcp.c:555): свойство монотонности тайм-аута от скорости (чем выше baud — тем меньше timeout) проверяемо для всех стандартных значений {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200}.
- [`validate_hostname()`](main/setting_validators.c:15) / [`validate_ssid()`](main/setting_validators.c:55): fuzz-тест на случайные UTF-8 строки длиной 0-65 байт — не должны крашить.
- [`crc_check()`](main/bridge/sniffer.c:98): свойство `crc_check(buf_with_correct_crc, len) == true` для любого валидного Modbus RTU фрейма.

### Test-data factories (необходимы)
- `make_modbus_tcp_frame(tid, unit_id, fc, data)` — нужен для `modbus_tcp` и `cache_modbus_server` тестов
- `make_modbus_rtu_frame(slave, fc, data)` — нужен для `sniffer` и `cache_multimaster` тестов
- `make_settings_json(groups...)` — нужен для `settings_manager` тестов

### Contract-тесты
Не применимы: все external API покрыты существующими QEMU api_tests.

### Дымовые нагрузочные тесты
- Cache lookup с `count=2000` FC01 должен ответить < 5 с (сейчас потенциально ~40 с, **Bug #9**)

---

## 4. Обнаруженные антипаттерны

| Место | Проблема |
|-------|----------|
| [`tcp_server_run_receiver_for_test()`](main/bridge/tcp_server.c:412) | Shim расходится с production: `break` на ошибке вместо `close_handler` |
| [`unittests/tcp_server/tcp_server_test.c:216`](unittests/tcp_server/tcp_server_test.c:216) | `free(desc)` напрямую вместо `tcp_server_deinit()` — обходит cleanup |
| [`unittests/tcp_server/mocks/lwip/sockets.c:95`](unittests/tcp_server/mocks/lwip/sockets.c:95) | Sentinel `-2` для mock_accept_fd + имплицитный errno — order-dependent |
| [`unittests/modbus_tcp/modbus_tcp_test.c:271`](unittests/modbus_tcp/modbus_tcp_test.c:271) | `mock_packet_queue_reset()` в середине теста — маскирует double-push |
| [`unittests/sniffer/mocks/freertos/timers.h:10`](unittests/sniffer/mocks/freertos/timers.h:10) | `xTimerStop` — no-op inline, call count невидим |
| [`unittests/sniffer/mocks/freertos/timers.h:17`](unittests/sniffer/mocks/freertos/timers.h:17) | `pvTimerGetTimerID` всегда возвращает `0` — тесты порта 1 vacuously pass |
| [`unittests/setting_validators/setting_validators_test.c:350`](unittests/setting_validators/setting_validators_test.c:350) | `test_validate_password("")==false` — тавтологичный тест, маскирует **Bug #5** |
| [`main/frontend/src/utils/snifferUtils.test.ts:23`](main/frontend/src/utils/snifferUtils.test.ts:23) | `new Date(2024, 0, 1, 12, 34...)` — локально-зависимый тест, падает в UTC CI |
| [`api_tests/20_test_cache_tcp_framing.py:142`](api_tests/20_test_cache_tcp_framing.py:142) | `time.sleep(1)` вместо retry-loop — нестабилен на медленном CI |
| [`modbus_tcp_test_init_ctx`](main/bridge/modbus_tcp.c:728) | `memset(ctx, 0)` → `pending_client_sock==0`, а не `-1` как в production |
| [`pm_lock()`](main/bridge/port_manager.c:47) | Lazy mutex creation — нетестируемый God-синглтон-антипаттерн |
| [`settings_manager.c`](main/settings_manager.c) | Полное отсутствие unit-тестов для наиболее сложного модуля |

---

## 5. Необходимые рефакторинги перед написанием тестов

| ID | Рефакторинг | Файл | Блокирует |
|----|------------|------|-----------|
| R-TCP-1 | Унифицировать error-path в test-shim с production (add `close_handler` call) | [`tcp_server.c:412`](main/bridge/tcp_server.c:412) | U1 |
| R-TCP-2 | Добавить `mock_send_return_override` в sockets mock | [`unittests/tcp_server/mocks/lwip/sockets.c`](unittests/tcp_server/mocks/lwip/sockets.c) | U4 |
| R-TCP-3 | Добавить `mock_vTaskDelay_callback` hook в freertos/task mock | [`unittests/mocks/freertos/task.c`](unittests/mocks/freertos/task.c) | U6, I3 |
| R-TCP-4 | Добавить notification hook при смерти acceptor (`EVENT_TASK_FINISHED` или callback) | [`tcp_server.c:221`](main/bridge/tcp_server.c:221) | U7 (positive assert) |
| R-TCP-5 | Per-call `xTaskCreate` failure control в mock | [`unittests/mocks/freertos/task.c`](unittests/mocks/freertos/task.c) | U9 |
| **R-TCP-6** ⚠️ | **Заменить `int active_connections` на `_Atomic int`** | [`tcp_desc.h:24`](main/bridge/tcp_desc.h:24), [`tcp_server.c:182`](main/bridge/tcp_server.c:182) | Bug #14 fix |
| R-MBTCP-1 | Добавить shims для чтения `pending_*` полей | [`modbus_tcp_internal.h`](main/bridge/modbus_tcp_internal.h) | MBTCP-U-025..030 |
| R-MBTCP-2 | Добавить shim для `make_rtu_request_from_tcp` | [`modbus_tcp.c:430`](main/bridge/modbus_tcp.c:430) | MBTCP-U-028 |
| R-MBTCP-3 | Добавить shim для `process_data_from_serial` | [`modbus_tcp.c:162`](main/bridge/modbus_tcp.c:162) | MBTCP-I-001..003 |
| **R-MBTCP-4** ⚠️ | **Добавить mutex/`_Atomic` на `pending_tid/slave_id/client_sock`** | [`modbus_tcp.c:332-349`](main/bridge/modbus_tcp.c:332) | Bug #2 fix |
| **R-PM-1** ⚠️ | **Создавать mutexes в `port_manager_init`, убрать lazy init** | [`port_manager.c:45`](main/bridge/port_manager.c:45) | Bug #3 fix, U1 |
| **R-PM-2** ⚠️ | **NVS сохранять только после успешного `port_init_mode`** | [`port_manager.c:332`](main/bridge/port_manager.c:332) | Bug #13 fix, U2 |
| **R-PM-3** ⚠️ | **Обернуть get+use `serial_desc_t*` под `pm_lock`** | [`port_manager.c:319`](main/bridge/port_manager.c:319) | Bug #11 fix, U3 |
| R-PM-4 | Добавить `mock_serial_set_tx_disabled_called` в serial mock | [`unittests/port_manager/mocks/serial.c`](unittests/port_manager/mocks/serial.c) | U3 |
| **R-SM-1** ⚠️ | **Объявить `settings_process_request_json` в `settings_manager.h`** | [`settings_manager.h`](main/settings_manager.h) | Все SM-U/I тесты |
| R-SM-2 | Injectable NVS-mock с per-call failure control | новый `unittests/settings_manager/mocks/setting_items.c` | SM-U-001, SM-U-004 |
| **R-SM-3** ⚠️ | **Разделить `validate_password` → `validate_login_password` + `validate_wifi_password`** | [`setting_validators.c:338`](main/setting_validators.c:338) | Bug #5 fix, SM-U-003 |
| **R-SM-4** ⚠️ | **Range-check double перед `(int)valuedouble`** | [`settings_manager.c:206`](main/settings_manager.c:206), [`:128`](main/settings_manager.c:128) | Bug #15 fix, SM-U-002 |
| R-CMS-1 | Добавить shim для `frame_total_len` | [`cache_modbus_server.c:402`](main/bridge/cache_modbus_server.c:402) | CMS-U-NEW-2 |
| **R-CMS-2** ⚠️ | **Отклонять 9+ соединение вместо `process_one_frame(raw_buf)`** | [`cache_modbus_server.c:413`](main/bridge/cache_modbus_server.c:413) | Bug #7 fix, CMS-U-NEW-1 |
| R-CM-1 | Добавить batch API `cache_multimaster_lookup_range` | [`cache_multimaster.c:406`](main/bridge/cache_multimaster.c:406) | Bug #9 fix |
| **R-CM-2** ⚠️ | **Заменить `vTaskDelete` на stop-flag + join** | [`cache_multimaster.c:241`](main/bridge/cache_multimaster.c:241) | Bug #10 fix |
| **R-SN-1** ✅ | **Переместить `should_stop_timer=true` ДО `goto exit_critical`** | [`sniffer.c:484`](main/bridge/sniffer.c:484) | Bug #8 fix, SN-U-NEW-1 |
| **R-SN-2** ✅ | **Добавить `classify_direction` guard перед pairing block** | [`sniffer.c:512`](main/bridge/sniffer.c:512) | Bug #17 fix, SN-U-NEW-2 |
| R-SN-3 | Добавить `mock_xTimerStop_called` в freertos timers mock | [`unittests/mocks/freertos/timers.c`](unittests/mocks/freertos/timers.c) | SN-U-NEW-3 |
| **R-FE-1** ⚠️ | **Добавить presence/type guards в `parsePacket`** | [`snifferUtils.ts:138`](main/frontend/src/utils/snifferUtils.ts:138) | FE-U-001, FE-U-002 |
| **R-FE-2** ⚠️ | **Вынести `findDeepest` в `packetDecoderUtils.ts` с depth cap** | [`PacketDecoder.vue:110`](main/frontend/src/components/PacketDecoder.vue:110) | FE-U-003 |
| **R-FE-3** ✅ | **Хранить reconnect handle в ref, вызывать `clearTimeout` в `onUnmounted`** | [`Sniffer.vue:77`](main/frontend/src/views/Sniffer.vue:77) | Bug #18 fix, FE-I-001 |
| **R-FE-4** ✅ | **Добавить `isMutating` ref в `toggleCaching`/`resetMap`** | [`RegisterMap.vue:104`](main/frontend/src/views/RegisterMap.vue:104) | Bug #19 fix, FE-I-002 |
| **R-FE-5** ✅ | **`clearInterval` в начале true-ветви watcher `cacheEnabled`** | [`RegisterMap.vue:300`](main/frontend/src/views/RegisterMap.vue:300) | Bug #20 fix, FE-I-003 |

---

## 6. План внедрения (по фазам)

### Фаза 1 — «Быстрые wins» и критические регрессии (1-2 недели)

**ROI: высокий, усилие: малое. Исправления однострочные или двустрочные.**

1. **R-SN-1** — исправить `goto exit_critical` в sniffer.c (1 строка) + **SN-U-NEW-1** (Bug #8)
2. **R-FE-3 + R-FE-4 + R-FE-5** ✅ — три однострочных frontend-фикса + **FE-I-001..003** (Bugs #18, #19, #20)
3. **R-SM-3** — разделить `validate_password` (8 строк) + обновить `setting_items.c` + **SM-U-003/003b** (Bug #5)
4. **R-SM-4** — range-check double (4 строки) + **SM-U-002** (Bug #15)
5. **SM-U-005** — `settings_build_response_json` wifi_perm_disable rendering

### Фаза 2 — Тест-инфраструктура и пустые сюиты (1-2 недели)

**ROI: высокий благодаря разблокировке последующих фаз. Усилие: среднее.**

1. **R-SM-1** — объявить `settings_process_request_json` в header
2. **R-SM-2** — injectable NVS-mock
3. Создать `unittests/settings_manager/` сюит с **SM-U-001, SM-U-004, SM-I-001, SM-I-002**
4. **R-MBTCP-1, R-MBTCP-2, R-MBTCP-3** — shims для modbus_tcp
5. **MBTCP-U-025..030, MBTCP-I-001..003** (Bug #2)
6. **R-FE-1, R-FE-2** + **FE-U-001..003**

### Фаза 3 — Критические системные дефекты (2-3 недели)

**ROI: очень высокий, но требует более сложных рефакторингов.**

1. **R-PM-1** — eager mutex init (Bug #3) + **U1**
2. **R-PM-2** — NVS rollback (Bug #13) + **U2, I1**
3. **R-PM-3 + R-PM-4** — lock serial pointer (Bug #11) + **U3**
4. **R-TCP-1..R-TCP-5** — mock улучшения + **U1..U10** для tcp_server
5. **R-TCP-6** — `_Atomic int active_connections` (Bug #14) + **I1**

### Фаза 4 — Cache и sniffer доработки (1-2 недели)

1. **R-CMS-1, R-CMS-2** — Bug #7 fix + **CMS-U-NEW-1, CMS-U-NEW-2**
2. **R-CM-1** — batch lookup API (Bug #9) + performance E2E-тест
3. **R-CM-2** — stop-flag pattern (Bug #10) + **CM-U-NEW-4**
4. **R-SN-2, R-SN-3** — Bug #17 fix + **SN-U-NEW-2..5**
5. E2E тесты: tcp_server reconnect (Bug #1, #6), sniffer WS (Bugs #8, #17)

---

## 7. Источники

- Отчёты 7 аналитиков (модули: tcp_server, modbus_tcp, port_manager, settings_manager+validators, cache_modbus_server+multimaster, sniffer, frontend)
- Верификация: `make unittests` — 30 сюитов, ~560 C unit-тестов, 0 отказов
- Верификация frontend: `npm run test` — 337 тестов Vitest, 0 отказов
- Входной список: 20 Critical/High/Medium дефектов + 11 Low/fragility-notes

**Статистика фильтрации:**
- Шаг 1 (dedup across modules): удалено 4 пересекающихся E2E предложения
- Шаг 2 (skip-list): удалено 18 предложений (trivial/wiring/passthrough)
- Шаг 3 (pyramid budget): итого unit=42 (76%), integration=14 (25%), E2E=17 (без учёта существующих) — удалено 4 E2E выше лимита
- Шаг 4 (refactor-blocking): 22 теста помечены как требующие предварительного рефакторинга
- Шаг 5 (adversarial check): отклонено 3 теста, чьи assertions не поймали бы реальные поломки

---

*Отчёт сгенерирован Test Suite Orchestrator, 2026-05-27*
