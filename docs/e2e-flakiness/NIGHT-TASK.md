# Ночная задача: эксперименты с e2e-тестами wb-mge (автономно, до утра)

Ты — ночной агент на своей VM. Контекста у тебя нет — здесь всё необходимое. Работаешь полностью автономно у себя; никакие внешние машины (лаборатория, CI) тебе не нужны и недоступны. Хозяин спит — вопросов не задавать, решения принимать самому, всё фиксировать в отчёте.

## Код и материалы

Репозиторий публичный: `https://github.com/wirenboard/wb-mge.git`

- Ветка **`fix/vvzvlad-tcp-server-deinit-hang`** — ты сейчас читаешь файл из неё:
  - голова `da53a4b` — WIP-фикс deinit-хэнга (для программы E);
  - её родитель **`7c275522ef`** — БАЗОВЫЙ срез, на котором сделаны все замеры ниже. Программы A и B гоняй строго на нём: `git checkout 7c275522ef`.
  - `docs/e2e-flakiness/` — этот файл, `results/` (JUnit-отчёты эталонных прогонов: `full-1..3.xml` — свободная машина, `starved-1..2.xml` — ровный троттлинг 0.4 CPU), `scripts/` (харнесс запуска серий и нагрузочный харнесс — образцы, адаптируй пути под себя).
- Ветка `fix/vvzvlad-unittests-parallel-race` — к e2e не относится (фикс гонки в юнит-тестовых makefile; в базовом срезе `make -j unittests` сломан — юнит-тесты не гоняй вообще).

## Шаг 0: развернуть стенд (~20-30 мин)

```bash
git clone https://github.com/wirenboard/wb-mge.git ~/wb-mge
cd ~/wb-mge && git checkout 7c275522ef        # baseline для программ A и B
docker build -t wb-mge-devenv .               # ~12 мин, тянет espressif/idf:v5.4.4
# обе проверки обязаны пройти:
docker run --rm wb-mge-devenv bash -lc 'find /opt/esp/tools -name qemu-system-xtensa | head -1'
docker run --rm wb-mge-devenv bash -lc '/opt/api_tests_venv/bin/python -m pytest --version'
```

Запуск тестов (первый прогон дольше: npm-сборка фронтенда и докачка managed_components — нужна сеть до registry.npmjs.org и components.espressif.com):

```bash
docker run --rm --name mge-night -v ~/wb-mge:/root/esp/project -w /root/esp/project \
  wb-mge-devenv bash -lc 'source /opt/esp/idf/export.sh && make qemu-test [PYTEST_ARGS="..."]'
```

Перед стартом проверь диск (`df -h /`): образ займёт ~17 ГБ, плюс кэши сборки.

## Грабли (каждая уже стоила часа, не наступай)

1. **PYTEST_ARGS — пути относительно `api_tests/`** (рецепт делает `cd api_tests`). `PYTEST_ARGS="api_tests/13_test_ports.py"` молча найдёт НОЛЬ тестов (exit 4, «no tests ran») и выглядит как успешный запуск. Всегда проверяй, что тесты реально собраны.
2. **Два QEMU одновременно не поднимутся**: порты захардкожены (8080, 8081, 50502-4, 5561-2, udp 5570), а `conftest._check_no_stale_qemu()` роняет прогон при чужом `qemu-system-xtensa`. Один прогон за раз; порты на твоей VM должны быть свободны.
3. Главный диагностический инструмент — **лог прошивки** `build/qemu_test.log`. Перезаписывается каждым прогоном — забирай сразу после падения. JUnit: `build/qemu_test_report.xml`.
4. Дефолтный таймаут теста 180 с (`pytest.ini`); reboot-тесты автоматически уезжают в конец сессии.
5. Нагрузочные крутилки помечай `# WBHOG` в командной строке и чисти trap-ом И вручную после каждого шага: `pgrep -f WBHOG` пуст, `docker ps` пуст. И помни: `pkill -f WBHOG` из ssh-однострочника убивает сам ssh-сеанс (паттерн матчится на собственную командную строку) — либо `bash -s` со скриптом через stdin, либо паттерн `"WBHO[G]"`.
6. Git-идентичность прогона видна в тесте `02_test_info` — на чистом клоне он должен проходить; если падает на git_info, значит `.git` потерялся.

## Контекст задачи

e2e-сюит — 229 pytest-тестов против прошивки ESP32, крутящейся в QEMU (TCG, однопоточный гость). На CI (Jenkins-нода с 12 executors, вечная толкучка за CPU) сюит шёл ~86 минут, и в 8 последних сборках стабильно падали 18 тестов + 14 плавали (в последней — 29 падений). Из-за этого e2e из CI выключили, а тесты продолжали писать вслепую.

План хозяина: (1) довести сюит до стабильно зелёного на свободной машине — по каждому падению решить: чинить прошивку, чинить тест или отключать; (2) потом шардировать для скорости; (3) флаки сборочной машины прицельно исследовать нагрузкой и найти границу воспроизведения. Твоя ночь — пункт (3) и подготовка данных для (1).

## Что уже твёрдо установлено — НЕ переделывай

Замеры на срезе `7c275522ef` (падений из 229; на копиях без .git падал ещё артефактный `02_test_info` — на твоём клоне его быть не должно):

| Среда | Время | Падений |
|---|---|---|
| мак хозяина (реалтайм-эмуляция), ×3 | ~23 мин | 3, 2, 3 |
| свободный сервер 8 ядер, ×3 | ~31 мин | 4, 4, 5 |
| тот же сервер, cpulimit 0.4 (ровный троттлинг), ×2 | ~55 мин | 6, 6 |
| CI, сборка #102 | ~86 мин | 29 |

**Ключевое открытие тебе на вход**: ровное замедление почти не воспроизводит CI (+2 теста), а **CPU-конкуренция** (хоги, ~3 на ядро) воспроизводит CI-сигнатуру мгновенно: `32_test_transparent_sniffer.test_transparent_tx_disabled_port2` под 24 хогами на 8 ядрах падает за 20 секунд, повиснув на первом же `GET /settings`. Сигнатура каскада CI та же: питон висит в `http/client.py:_read_status → socket.readinto`, `Failed: Timeout from pytest-timeout`; data-path тесты получают РОВНО ноль байт (`got=''`), никогда частично. При 64 хогах на 8 ядрах вылезает уже другой отказ (`set_port_mode → 409`). Дело в джиттере планировщика, профиль подбирается. Масштабируй под свои ядра: базовый профиль `3 × nproc` хогов.

Вердикты по уже разобранным падениям (время на них не трать):

1. `13_test_ports.test_clock_out_keeps_rs485_2_de_low` — **тест не прав**: проверяет RTS-поведение DE-линии, которого под QEMU нет по построению (`main/bridge/serial.c`, `#if QEMU_BUILD` → `UART_MODE_UART`). Никогда не проходил.
2. `13_test_ports.test_sniffer_status_both_ports_independent` — **баг прошивки**: WS-сессия снифера одна на устройство, второе подключение вышибает первое (лог: `WS client replaced, closing previous session`), `stop` в мёртвый сокет теряется.
3. `42_*.test_dual_port_merge_most_recent_wins_and_pool_survival` — **тест не прав**: проверяет порт-мерженный пул кэша, отвергнутый review #51 (лог прошивки: `the cache is single-port (review #51)`).
4. `21_*.test_gateway_multiconn_concurrent_split_frames` — **осознанный компромисс прошивки**: `conn_generation` на дескриптор, не на fd (комментарий в `tcp_server_send_to_captured_client()`, `main/bridge/tcp_server.c`); отвал чужого клиента инвалидирует ответ живому. Тест без ретраев → флак, частота растёт с замедлением.
5. `00_test_heap_session.test_heap_no_leak` — **производный**: зелёный при ≤27 падениях в прогоне, красный при ≥28. Отдельно не копать.
6. **deinit-хэнг** (`37_test_cache_server_deinit_hang`, в CI 7/8): в `tcp_server_deinit()` (`main/bridge/tcp_server.c` ~943, ~951 на базовом срезе) два неограниченных ожидания, вызываются из HTTP-хендлера настроек (`settings_update.c:69 → cache_modbus_server.c:438`). Гипотеза: один хэнг вешает HTTP навсегда → каскад падений с сигнатурой выше. Прошивка НЕ крашится (в qemu_test.log нет Guru Meditation / watchdog / assert). Docstring теста 37 УСТАРЕЛ (EAGAIN-путь давно починен) — корневая причина зависания НЕ поймана, её ловля — программа C.
7. В CI все 6 падений `31_*` — это `failed on setup`: module-scoped фикстура виснет на `api.set_port_mode(1,"passive")`. Один повисший HTTP-вызов размножается на 6 «падений».

Список CI-18 (падали во всех 8 сборках CI):

```
13_test_ports.test_clock_out_keeps_rs485_2_de_low
13_test_ports.test_sniffer_status_both_ports_independent
28_test_gateway_unit_id_protocol.test_gateway_invalid_protocol_id_drops_frame
28_test_gateway_unit_id_protocol.test_gateway_nonzero_unit_id_passthrough
31_test_cache_modbus_server_e2e.test_cm01_fc01_coil_response_lsb_bit_packing
31_test_cache_modbus_server_e2e.test_cm02_fc02_discrete_vs_coil_type_separation
31_test_cache_modbus_server_e2e.test_cm03_fc04_input_vs_holding_type_separation
31_test_cache_modbus_server_e2e.test_cm04_unsupported_fc_returns_illegal_function
31_test_cache_modbus_server_e2e.test_cm05_fc03_count_boundary_illegal_data_value
31_test_cache_modbus_server_e2e.test_cm07_cache_miss_returns_illegal_address
32_test_transparent_sniffer.test_sniffer_ws_disconnect_firmware_stays_alive
32_test_transparent_sniffer.test_transparent_tx_disabled_port2
35_test_sniffer_cache_interaction.test_sniffer_after_register_map_cache_toggle
35_test_sniffer_cache_interaction.test_sniffer_works_after_cache_overlay_cycle
35_test_sniffer_cache_interaction.test_sniffer_works_with_cache_overlay_enabled
35_test_sniffer_cache_interaction.test_sniffer_ws_restart_after_cache_disable_while_active
35_test_sniffer_cache_interaction.test_sniffer_ws_works_after_other_port_cache_cycle
46_test_io_direction_tx_disabled.test_dir_pin_parked_blocks_uart
```

CI-плавающие (доля из 8 сборок): `37_*` 7/8, `38_*_fast_response_master_slave_pair` 7/8, `33_*_au05_full_buffer_preserved_after_sw_reboot` 7/8, `29_*_dual_port_simultaneous` 5/8, `00_heap_no_leak` 5/8, `25_*_zero_bytes` 4/8, `25_*_single_client_cap_block_new` 4/8, `25_*_serial_to_tcp_client_never_sent` 4/8, `32_*_port2_basic_roundtrip` 4/8, `32_*_sniffer_to_tcp_bridge_data_path_restored` 4/8, `21_*_multiconn` 3/8, `25_*_basic_roundtrip` 3/8, `03_test_settings.test_per_field_validation` 2/8, `47_*_factory_reset_long_press` 1/8.

## Ночная программа (по приоритету; baseline = `7c275522ef`)

Сначала **один полный прогон без нагрузки** — твоя собственная база (железо иное, сравнивай наборы, не абсолюты). Ожидание: упадут ~3-5 из ядра выше.

### A. Классификация групп (главное, ~2-3 ч)

Для каждого: `31_test_cache_modbus_server_e2e.py`, `35_test_sniffer_cache_interaction.py`, `28_test_gateway_unit_id_protocol.py`, `38_test_sniffer_slow_response.py`, `33_test_auth_settings.py -k test_au05`, `25_test_transparent_tcp_e2e.py`, `46_test_io_direction_tx_disabled.py`, `29_test_gateway_dual_port.py`, `03_test_settings.py -k test_per_field_validation`:

- изолированно БЕЗ нагрузки ×3;
- изолированно под хогами (`3×nproc`) ×3 — образец харнесса в `docs/e2e-flakiness/scripts/run-under-load.sh`.

Итог — таблица: тест → `детерминированный` / `джиттерный` (что именно виснет: traceback + строки qemu_test.log) / `зелёный изолированно` (кандидат в каскадные). Это главный продукт ночи.

### B. Профиль нагрузки, воспроизводящий CI (~3-4 ч)

Полный сюит под хогами: `3×nproc`, потом меньше/больше по результату (ищи колено). Отчёты сохраняй с говорящими именами. Сравнивай наборы с CI-18. Если прогон завис на одном тесте >25 мин — это ПОЙМАННЫЙ ХЭНГ: НЕ убивая контейнер, сохрани `build/qemu_test.log` и `docker logs mge-night`, потом убей и зафиксируй, на каком тесте встало и последние 30 строк лога прошивки.

### C. Охота на deinit-хэнг (~1-2 ч, если A и B оставили время)

10-20 изолированных прогонов `36_test_tcp_server_deinit_hang.py 37_test_cache_server_deinit_hang.py` под разными профилями хогов. Каждый пойманный Timeout: qemu_test.log в `~/results/hang-<i>.log` + последние 30 строк лога прошивки в отчёт. Это путь к корневой причине дедлока.

### D. Опционально

Если `31_*` изолированно зелёные даже под нагрузкой (ожидаемо) — прогнать последовательность `33_test_auth_settings.py 35_test_sniffer_cache_interaction.py 31_test_cache_modbus_server_e2e.py` под нагрузкой: подтвердит каскад «повисший HTTP → размножение падений».

### E. Опционально: проверка WIP-фикса

`git checkout fix/vvzvlad-tcp-server-deinit-hang` (голова, `da53a4b`) — там ожидания deinit ограничены и `ESP_ERR_TIMEOUT` доезжает до HTTP-ответа. Повтори на нём лучший репро-сценарий из B/C: ожидание — вечный хэнг превращается в внятную ошибку HTTP, каскад исчезает. Результат сравнения — в отчёт (это валидация фикса, но помни: он WIP и корневую причину не чинит).

## Скрипт сравнения отчётов

```python
import xml.etree.ElementTree as ET, sys
def bad(f):
    t=ET.parse(f); s=set()
    for tc in t.iter("testcase"):
        if tc.find("failure") is not None or tc.find("error") is not None:
            s.add((tc.get("classname") or "").split(".")[-1]+"."+tc.get("name"))
    return s
print("\n".join(sorted(bad(sys.argv[1]))))
```

## Жёсткие ограничения

- Прошивку (`main/`) НЕ править. Тесты (`api_tests/`) — только для диагностики, каждый дифф в отчёт.
- Ничего не коммитить и не пушить.
- Наружу не ходить, кроме github / docker hub / npm / components.espressif.com при развёртывании.
- Хоги только с меткой WBHOG + очистка после каждого шага. К утру: хогов нет, контейнеров нет.

## Отчёт

Пиши ИНКРЕМЕНТАЛЬНО после каждого эксперимента в **`~/night-report.md`**: время, команда, результат, вывод одной строкой. XML-отчёты в `~/results/`. Раз в час-полтора шли короткую веху в ответ в чат. Финал ночи — сводная таблица классификации + два главных вывода: какой профиль нагрузки воспроизводит CI-набор и какие из CI-падений каскадные.
