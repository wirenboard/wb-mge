# Ночная задача: подготовить e2e-сюит wb-mge к CI/CD

Ты — автономный ночной агент на своей Linux-VM, контекста у тебя нет — здесь всё необходимое. Работаешь полностью локально: собираешь и гоняешь всё у себя, никакие внешние машины (лаборатория, CI, мак хозяина) не нужны и недоступны. Хозяин спит — вопросов не задавать, решения принимать самому, каждое решение фиксировать в отчёте.

## Миссия

Твоя задача — ТА ЖЕ, что была у дневного агента, целиком, а не «прогнать замеры»:

**разобраться, почему e2e-тесты флакают и какие именно; какие не работают вовсе и почему; починить то, что чинится — и тесты, и прошивку, по обоснованному вердикту; что не чинится — отключить с записанной причиной; в целом подготовить сюит к работе на CI/CD-машине.**

Критерии готовности (это и есть определение «сделано»):

1. По каждому падавшему тесту — вердикт: `тест не прав` / `баг прошивки` / `осознанный компромисс` / `чувствителен к нагрузке` — и что сделано: починен / отключён (skip с причиной) / оставлен, почему.
2. Полный сюит **стабильно зелёный ≥3 прогонов подряд** на свободной машине.
3. Для чувствительных к нагрузке — граница воспроизведения (профиль хогов) и рекомендация: поднять таймаут / чинить / skip на CI.
4. Все правки закоммичены в твою ветку, отчёт написан.

## Код и материалы

Репозиторий публичный: `https://github.com/wirenboard/wb-mge.git`

- **`7c275522ef`** — БАЗОВЫЙ срез: на нём сделаны все замеры ниже, от него отводи свою рабочую ветку.
- Ветка **`fix/vvzvlad-tcp-server-deinit-hang`** — ты читаешь файл из неё: голова — WIP-фикс deinit-хэнга (`da53a4b`, для программы 5), `docs/e2e-flakiness/` — этот файл, эталонные JUnit-отчёты в `results/` (`mac-1..3.xml` — реалтайм-эмуляция, `full-1..3.xml` — свободный сервер 8 ядер, `starved-1..2.xml` — ровный троттлинг 0.4 CPU), образцы харнесов в `scripts/`.
- Ветка **`fix/vvzvlad-unittests-parallel-race`** (`339ece7`) — фикс гонки `make -j unittests` (на базовом срезе юнит-тесты при `-j` недетерминированно теряют сюиты). **Черри-пикни его первым коммитом своей ветки** — иначе юнит-тестам верить нельзя.

Своя ветка: `git checkout -b fix/night-e2e-stabilization 7c275522ef && git cherry-pick 339ece7`. Коммить туда атомарно, по фиксу на коммит, комментарии в коде — только английские, сообщения — conventional commits. Если есть креды на push — пушь; если нет — в конце `git bundle create ~/night-work.bundle fix/night-e2e-stabilization` и путь в отчёт.

## Шаг 0: развернуть стенд (~20-30 мин)

```bash
git clone https://github.com/wirenboard/wb-mge.git ~/wb-mge
cd ~/wb-mge && git checkout -b fix/night-e2e-stabilization 7c275522ef
git cherry-pick 339ece7
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

Перед стартом проверь диск (`df -h /`): образ ~17 ГБ плюс кэши сборки.

## Грабли (каждая уже стоила часа, не наступай)

1. **PYTEST_ARGS — пути относительно `api_tests/`** (рецепт делает `cd api_tests`). `PYTEST_ARGS="api_tests/13_test_ports.py"` молча найдёт НОЛЬ тестов (exit 4, «no tests ran»). Всегда проверяй, что тесты реально собраны.
2. **Два QEMU одновременно не поднимутся**: порты захардкожены (8080, 8081, 50502-4, 5561-2, udp 5570), а `conftest._check_no_stale_qemu()` роняет прогон при чужом `qemu-system-xtensa`. Один прогон за раз.
3. Главный диагностический инструмент — **лог прошивки** `build/qemu_test.log`. Перезаписывается каждым прогоном — забирай сразу после падения. JUnit: `build/qemu_test_report.xml`.
4. Дефолтный таймаут теста 180 с (`pytest.ini`); у отдельных тестов маркеры жёстче (20-60 с). Reboot-тесты автоматически уезжают в конец сессии (`pytest_collection_modifyitems`).
5. Нагрузочные крутилки помечай `# WBHOG` в командной строке и чисти trap-ом И вручную после каждого шага: `pgrep -f "WBHO[G]"` пуст, `docker ps` пуст. `pkill -f WBHOG` из ssh-однострочника убивает сам ssh-сеанс (паттерн матчится на собственную командную строку) — используй `"WBHO[G]"`.
6. Git-идентичность прогона видна в тесте `02_test_info` — на чистом клоне он должен проходить; если падает на git_info, значит `.git` потерялся.
7. Не верь docstring-ам тестов на слово: docstring `37_test_cache_server_deinit_hang` описывает механизм, давно починенный в коде (EAGAIN-путь), — настоящая причина другая. Вердикт всегда через лог прошивки + чтение кода.

## Контекст

e2e-сюит — 229 pytest-тестов против прошивки ESP32 в QEMU (TCG, однопоточный гость). На CI (Jenkins-нода `build-node-1-vm`, 12 executors, вечная толкучка за CPU) сюит шёл ~86 минут; в 8 последних e2e-сборках (#94-102, ветка `feature/vvzvlad-caching-multimaster`) стабильно падали 18 тестов + 14 плавали (29 падений в #102). Из-за этого e2e из CI выключили (`RUN_E2E` по умолчанию false в Jenkinsfile), а тесты продолжали писать вслепую — отсюда тесты, не сходящиеся с кодом. Когда сюит писали активно, он был 100% зелёный на маке и на свободном сервере — то есть «стабильные 18» это смесь: часть — тесты против несуществующего поведения, часть — каскад от повисшего HTTP (см. ниже).

## Что уже твёрдо установлено — НЕ перемеряй, пользуйся

Замеры на срезе `7c275522ef`, падений из 229 (на копиях без `.git` дополнительно падал артефактный `02_test_info` — в таблице вычтен; на твоём клоне его быть не должно):

| Среда | Время | Падений | Отчёты |
|---|---|---|---|
| реалтайм-эмуляция (мак), ×3 | ~23 мин | 4, 3, 4 | `results/mac-*.xml` |
| свободный сервер 8 ядер, ×3 | ~31 мин | 4, 4, 5 | `results/full-*.xml` |
| тот же сервер, cpulimit 0.4 (ровный троттлинг), ×2 | ~55 мин | 6, 6 | `results/starved-*.xml` |
| CI, сборка #102 | ~86 мин | 29 | списки ниже |

Ядро, падающее ВЕЗДЕ (все 8 прогонов выше): `13_test_ports.test_clock_out_keeps_rs485_2_de_low`, `13_test_ports.test_sniffer_status_both_ports_independent`, `42_test_sniffer_cache_overlays_e2e.test_dual_port_merge_most_recent_wins_and_pool_survival`; почти везде — `21_test_gateway_e2e_multiconn.test_gateway_multiconn_concurrent_split_frames` (мак 2/3, сервер 3/3, CI 3/8).

**Ключевое открытие**: ровное замедление почти не воспроизводит CI (+2 теста при 2.4× замедлении), а **CPU-конкуренция** (хоги, ~3 на ядро) воспроизводит CI-сигнатуру мгновенно: `32_test_transparent_sniffer.test_transparent_tx_disabled_port2` под 24 хогами на 8 ядрах падает за 20 секунд, повиснув на первом же `GET /settings`. Сигнатура каскада CI та же: питон висит в `http/client.py:_read_status → socket.readinto`, `Failed: Timeout from pytest-timeout`; data-path тесты получают РОВНО ноль байт (`got=''`), никогда частично; прошивка при этом НЕ крашится (в qemu_test.log нет Guru Meditation / watchdog / assert). При 64 хогах вылезает другой отказ (`set_port_mode → 409`). Дело в джиттере планировщика; базовый профиль `3 × nproc` хогов, образец харнесса — `scripts/run-under-load.sh`.

**Теория каскада CI** (объясняет стабильность набора 18): `tcp_server_deinit()` (`main/bridge/tcp_server.c`, ~943 и ~951 на базовом срезе) содержит два неограниченных ожидания и вызывается из HTTP-хендлера настроек (`settings_update.c:69 → cache_modbus_server.c:438`). Один хэнг — HTTP мёртв навсегда — всё после падает детерминированно. Подтверждение: в CI все 6 падений `31_*` — это `failed on setup`, module-scoped фикстура виснет на `api.set_port_mode(1,"passive")`; единственный тест файла без этой фикстуры (`cm06`) в CI проходил.

### Готовые вердикты (проверены изолированными прогонами с чтением лога прошивки)

1. `13_test_ports.test_clock_out_keeps_rs485_2_de_low` — **тест не прав**: проверяет RTS-поведение DE-линии, которого под QEMU нет по построению (`main/bridge/serial.c` ~255: `#if QEMU_BUILD` → `UART_MODE_UART`, потому что QEMU не эмулирует RS485 half-duplex). Никогда не проходил ни в одной среде.
2. `13_test_ports.test_sniffer_status_both_ports_independent` — **прошивка против намерения теста**: WS-сессия снифера одна на устройство, второе подключение вышибает первое (лог: `WS client replaced, closing previous session`), `stop` в мёртвый сокет теряется, статус порта 1 остаётся true.
3. `42_*.test_dual_port_merge_most_recent_wins_and_pool_survival` — **тест не прав**: проверяет порт-мерженный пул кэша, отвергнутый на review #51 (лог прошивки: `Port[2]: taking the cache overlay over from port 1 — the cache is single-port (review #51)` + `Cache cleared`). Тест написан против отброшенной архитектуры.
4. `21_*.test_gateway_multiconn_concurrent_split_frames` — **осознанный компромисс прошивки**: `conn_generation` на дескриптор, не на fd (большой комментарий в `tcp_server_send_to_captured_client()`, `main/bridge/tcp_server.c`): отвал ЧУЖОГО клиента во время RTU-оборота инвалидирует ответ живому; цена по задумке — один Modbus-ретрай. Тест ретраев не делает.
5. `00_test_heap_session.test_heap_no_leak` — **производный**: по 8 CI-сборкам зелёный при ≤27 падениях в прогоне, красный при ≥28. Отдельно не копать — сам позеленеет.
6. `02_test_info.test_info_format_validation` — **артефакт стенда** (нет `.git` → пустой git_info). На чистом клоне не воспроизводится.
7. `32_*.test_transparent_tx_disabled_port2` — **чувствителен к нагрузке**, репродьюсер CI-хэнга (см. «ключевое открытие»). Чисто проходит за 16 с.
8. deinit-хэнг (`37_*`, CI 7/8): корневая причина НЕ поймана; чисто и под 24/64 хогами локально прошёл. WIP-фикс `da53a4b` — только защита в глубину (ожидания ограничены, `ESP_ERR_TIMEOUT` доезжает до HTTP-ответа, при таймауте дескриптор НАМЕРЕННО утекает — не «чини» утечку, там комментарий почему).

### Бисект-окна для семейства «got=''» (это регрессии, а не просто флак)

Онсет датируется по CI-матрице:
- с **#97**: `25_*_zero_bytes_edge_case`, `25_*_single_client_cap_block_new`, `32_*_port2_basic_roundtrip` — окно `8bdb4bd..140b082` (7 коммитов), главный подозреваемый `c46767e` «fix(bridge): stop a serial reply from landing in someone else's socket» (а тест `serial_to_tcp_client_never_sent` проверяет ровно противоположное поведение!);
- с **#98**: `25_*_serial_to_tcp_client_never_sent`, `25_*_basic_roundtrip`, `21_*_multiconn` — окно `140b082..ea98575` (3 коммита: `ea98575`, `4e01d88` «stop GET /info reading a freed TCP descriptor», `5101a86`).

Если эти тесты у тебя красные хоть иногда — бисект по окну (checkout коммита → изолированный прогон) дешевле гадания.

### CI-списки (для сверки наборов)

Падали во всех 8 CI-сборках («CI-18»):

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

CI-плавающие (доля из 8 сборок): `37_*deinit_with_active_polling` 7/8, `38_*_fast_response_master_slave_pair` 7/8, `33_*_au05_full_buffer_preserved_after_sw_reboot` 7/8, `29_*_dual_port_simultaneous` 5/8, `00_heap_no_leak` 5/8, `25_*_zero_bytes` 4/8, `25_*_cap_block_new` 4/8, `25_*_serial_to_tcp_client_never_sent` 4/8, `32_*_port2_basic_roundtrip` 4/8, `32_*_bridge_data_path_restored` 4/8, `21_*_multiconn` 3/8, `25_*_basic_roundtrip` 3/8, `03_*_per_field_validation` 2/8, `47_*_factory_reset_long_press` 1/8.

## Программа работ (по приоритету)

Сначала **один полный прогон без нагрузки** — твоя собственная база (железо иное: сравнивай наборы, не абсолюты). Ожидание: упадёт ядро из 3-4 тестов.

### 1. Починить уже разобранное (вердикты готовы — сразу чинить, ~2-3 ч)

a. `13_*_clock_out_keeps_rs485_2_de_low` — переписать под QEMU-наблюдаемое: сам смысл (меандр не должен достигать порта 2, прошивка сама паркует G15 в LOW — это делает `wb_test.c`, не UART) проверяем; выкинуть только baseline «G04 idle HIGH», который держится на RTS. Не выйдет — `skipif` с причиной и ссылкой на `serial.c` `#if QEMU_BUILD`.
b. `42_*_dual_port_merge...` — переписать под фактическую семантику single-port takeover (включение на порту 2 отбирает оверлей у порта 1 и чистит пул) или удалить с обоснованием. Проверь соседей по файлу `42_*` на ту же ложную предпосылку («порт-мерженный пул») — под троттлингом плавали ещё `test_cache_toggle_mid_traffic_serves_fresh_value` и `test_cache_overlay_persists_across_reboot`.
c. `21_*_multiconn` — добавить в тест один ретрай (это соответствует задокументированному компромиссу прошивки). Per-fd generation в прошивке ночью НЕ делать — большая правка.
d. `13_*_sniffer_status_both_ports_independent` — сначала реши, дизайн это или баг: посмотри, как фронтенд (`main/frontend/src/views/Sniffer.vue` и рядом) работает с WS — один сокет на оба порта или два. Если один — одиночная сессия задумана, и тест переписать на один WS с командами обоих портов. Если независимые сессии предполагались — это баг прошивки (`main/bridge/sniffer.c`, «WS client replaced»); почини аккуратно или skip с вердиктом «баг прошивки, требует решения по дизайну».

После каждого фикса: изолированный прогон затронутого файла ×2, юнит-тесты (`make -j unittests` — у тебя они рабочие после черри-пика).

### 2. Классифицировать и починить остальные CI-группы (~3-4 ч)

Для каждого из: `31_test_cache_modbus_server_e2e.py`, `35_test_sniffer_cache_interaction.py`, `28_test_gateway_unit_id_protocol.py`, `38_test_sniffer_slow_response.py`, `33_test_auth_settings.py -k test_au05`, `25_test_transparent_tcp_e2e.py`, `46_test_io_direction_tx_disabled.py`, `29_test_gateway_dual_port.py`, `03_test_settings.py -k test_per_field_validation`, `47_test_io_indication.py -k factory_reset`:

- изолированно БЕЗ нагрузки ×3;
- изолированно под хогами (`3×nproc`) ×3.

По результату каждого — вердикт и действие: детерминированно красный → корневая причина (лог прошивки + код + бисект-окна выше) → фикс или skip; красный только под нагрузкой → таблица «что именно виснет» (traceback + строки qemu_test.log) → таймаут/фикс/пометка; зелёный изолированно → каскадный кандидат, чинится фиксом deinit-хэнга, зафиксируй это.

### 3. Полный сюит зелёный ×3 подряд без нагрузки — главный деливерабл

Прогнать после фиксов. Не добился — задокументируй, что осталось и почему.

### 4. Нагрузочная граница (~2 ч)

Полный сюит под хогами `3×nproc`; потом меньше/больше — ищи колено, где начинаются падения. Сравнивай наборы с CI-18. Если прогон завис на одном тесте >25 мин — это ПОЙМАННЫЙ ХЭНГ: не убивая контейнер, сохрани `build/qemu_test.log` и `docker logs mge-night`, потом убей и зафиксируй тест + последние 30 строк лога прошивки. Дополнительно: серия изолированных `36_test_tcp_server_deinit_hang.py 37_test_cache_server_deinit_hang.py` ×10-20 под разными профилями — каждый пойманный Timeout это материал для корневой причины deinit-дедлока.

### 5. Опционально: валидация WIP-фикса

`git checkout da53a4b` (голова `fix/vvzvlad-tcp-server-deinit-hang`) — повтори лучший репро-сценарий из п.4: ожидание — вечный хэнг превращается во внятную HTTP-ошибку, каскад исчезает. Результат в отчёт (фикс WIP, корневую причину не чинит).

### 6. Отчёт

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

- Править можно и тесты, и прошивку — но каждый дифф с вердиктом-обоснованием в отчёте; тест правится только когда доказано (логом прошивки/кодом), что не прав именно он.
- Коммиты — только в свою ветку `fix/night-e2e-stabilization`. Чужие ветки (особенно `feature/vvzvlad-caching-multimaster`) не трогать. Force-push никуда.
- Наружу не ходить, кроме github / docker hub / npm / components.espressif.com при развёртывании.
- Хоги только с меткой WBHOG + очистка после каждого шага. К утру: хогов нет, лишних контейнеров нет.

## Отчёт

Пиши ИНКРЕМЕНТАЛЬНО после каждого эксперимента в **`~/night-report.md`**: время, команда, результат, вывод одной строкой. XML-отчёты в `~/results/` с говорящими именами. Раз в час-полтора шли короткую веху в чат. Финал ночи:

1. Сводная таблица: тест → вердикт → действие (коммит/skip/оставлен) → статус.
2. Результат трёх чистых полных прогонов после фиксов.
3. Профиль нагрузки, воспроизводящий CI-набор, и список каскадных падений.
4. Ветка с коммитами (или путь к bundle) + любые пойманные логи хэнгов.
