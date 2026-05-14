# WB-MGE API Tests

## Описание

Автоматические интеграционные тесты для HTTP API устройства WB-MGE. Тесты покрывают:

- ✅ **Авторизация** — правильные/неправильные логин/пароль
- ✅ **Информация об устройстве** — структура, типы данных (включая heap, PSRAM, cache)
- ✅ **Настройки** — чтение, запись, валидация ограничений
- ✅ **Управление сессией** — logout, проверка сессии
- ✅ **Время работы** — uptime
- ✅ **Параметры Modbus TCP** — настройки bridge
- ✅ **Валидация параметров** — hostname, WiFi, порты
- ✅ **WiFi сканер** — запуск сканирования, получение результатов
- ✅ **Клиенты AP** — список подключенных устройств
- ✅ **Статические файлы** — HTML, CSS, JS, favicon
- ✅ **Команды** — set_default_settings
- ✅ **Hostname** — GET /hostname
- ✅ **Cache эндпоинты** — /cache/status, /cache/csv, /cache/json
- ✅ **Cache multimaster** — мультимастерный Modbus TCP сервер кэша
- ✅ **Защита доступа** — проверка авторизации для защищённых эндпоинтов

---

## Запуск на реальном устройстве

### 1. Установить зависимости

```bash
pip install -r api_tests/requirements.txt
```

### 2. Запустить тесты

```bash
# С указанием IP устройства
python api_tests/test_api.py --ip 192.168.5.1

# По умолчанию (192.168.5.1 — AP-режим)
python api_tests/test_api.py
```

**Дополнительные опции:**
```bash
# Остановиться на первой ошибке
python api_tests/test_api.py --ip 192.168.5.1 --stop-on-failure

# Подробный вывод
python api_tests/test_api.py --ip 192.168.5.1 --verbose
```

---

## Запуск с QEMU (e2e тесты без реального устройства)

QEMU позволяет запускать прошивку в эмуляторе и тестировать HTTP API без физического устройства. Все особенности оборудования замокированы:

- **WiFi**: сканирование возвращает две фиктивные сети (`QEMU-TestNetwork-1`, `QEMU-TestNetwork-2`)
- **RS-485 / Modbus RTU**: mock-задача инжектирует синтетические пакеты в sniffer для заполнения кэша
- **Cache Modbus TCP сервер**: работает на порту 50504 (QEMU пробрасывает `localhost:50504 → ESP32:50504`)

### 1. Собрать прошивку и запустить QEMU

```bash
source ~/.espressif/tools/activate_idf_v5.4.sh
make build-frontend build-qemu && ./run_qemu_with_web.sh
```

Это автоматически:
- Соберёт frontend
- Соберёт прошивку с QEMU-специфичной конфигурацией
- Запустит QEMU с проброской портов:
  - `localhost:8080 → ESP32:80` (HTTP API)
  - `localhost:50504 → ESP32:50504` (Modbus TCP cache)

Дождитесь сообщения в консоли QEMU:
```
I (XXXX) http_server: HTTP server started on port: 80
```

### 2. Запустить тесты в отдельном терминале

```bash
# Создать виртуальное окружение (если ещё нет)
python3 -m venv .venv
.venv/bin/pip install -r api_tests/requirements.txt

# Запустить все 16 тестов
.venv/bin/python api_tests/test_api.py --ip localhost:8080
```

### Ожидаемый результат

```
Running WB-MGE API tests
========================================
...
============================================================
TEST RESULTS:
✅ Passed: 16
❌ Failed: 0
📊 Total: 16

🎉 ALL TESTS PASSED SUCCESSFULLY!
```

### Особенности QEMU-прогона

| Тест | Поведение в QEMU |
|------|-----------------|
| `test_wifi_scanner` | Сразу завершается с 2 фиктивными сетями |
| `test_cache_multimaster` | Переключает порт 1 в `cache_bus`, ждёт заполнения кэша (~2с), подключается к `localhost:50504` |
| `test_commands` | `set_default_settings` сбрасывает настройки — тест стоит последним |

---

## Структура тестов

Все тесты — функции `test_xxx(api)` в [`api_tests/test_api.py`](api_tests/test_api.py). Класс `WBMGEAPI` предоставляет HTTP-клиент с методами для каждого эндпоинта.

Для добавления нового теста:
1. Добавьте функцию `def test_new_feature(api):`
2. Используйте `api.session.get(...)` или методы класса `WBMGEAPI`
3. Добавьте вызов в список `tests` в функции `main()`

```python
def test_new_feature(api):
    response = api.session.get(f"{api.base_url}/new_endpoint", timeout=10)
    assert response.status_code == 200
    data = response.json()
    assert "expected_field" in data
    print("✓ New feature works")
```
