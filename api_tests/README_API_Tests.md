# WB-MGE API Tests

## Описание

Автоматические интеграционные тесты для HTTP API устройства WB-MGE. Используют **pytest** + **pytest-order** для упорядоченного запуска. Тесты покрывают:

- **Авторизация и сессии** — логин, logout, смена пароля, защита эндпоинтов
- **Информация об устройстве** — структура, типы данных (heap, PSRAM, cache), форматы полей
- **Настройки** — чтение, запись, валидация, частичное обновление
- **Modbus TCP** — параметры bridge, валидация лимитов
- **WiFi** — сканирование, edge cases, поля сетей, клиенты AP
- **Cache** — /cache/status, /cache/csv, /cache/json, toggle сервера, multimaster
- **Порты** — режимы портов, sniffer, WB test endpoint
- **Прочее** — uptime, hostname, статические файлы, HTTP method guard, команды
- **Reboot** — перезагрузка устройства с проверкой uptime

---

## Структура файлов

```text
api_tests/
├── conftest.py          # pytest-фикстуры (api-клиент, --ip, проверка соединения)
├── api_client.py        # Класс WBMGEAPI — HTTP-клиент для всех эндпоинтов
├── modbus_helpers.py    # Утилиты Modbus TCP (encode/decode, worker-потоки, staleness)
├── pytest.ini           # Конфигурация pytest
├── requirements.txt     # Зависимости
│
├── test_auth.py         # Авторизация, сессии, смена пароля (order 1,2,6,25)
├── test_info.py         # Информация об устройстве (order 3,4)
├── test_settings.py     # Настройки, валидация, partial update (order 5,10,24)
├── test_uptime.py       # Uptime (order 7)
├── test_modbus.py       # Modbus TCP параметры (order 8,9)
├── test_wifi.py         # WiFi сканер, edge cases, AP clients (order 11-14)
├── test_static_files.py # Статические файлы (order 15)
├── test_http.py         # HTTP method guard (order 16)
├── test_commands.py     # Команды set_default_settings (order 17,18)
├── test_hostname.py     # Hostname endpoint (order 19)
├── test_cache.py        # Cache эндпоинты, multimaster (order 20-23,30)
├── test_sniffer_ws.py   # WebSocket sniffer (order 26)
├── test_ports.py        # Порты, sniffer, WB test (order 27-29)
└── test_reboot.py       # Reboot — всегда последний (order 31)
```

---

## Запуск на реальном устройстве

### 1. Установить зависимости

```bash
pip install -r api_tests/requirements.txt
```

### 2. Запустить тесты

```bash
# Все тесты с указанием IP
pytest api_tests/ --ip 192.168.5.1

# По умолчанию (192.168.5.1 — AP-режим)
pytest api_tests/
```

**Дополнительные опции:**

```bash
# Остановиться на первой ошибке
pytest api_tests/ --ip 192.168.5.1 -x

# Запустить только конкретный файл
pytest api_tests/test_auth.py --ip 192.168.5.1

# Запустить конкретный тест по имени
pytest api_tests/ --ip 192.168.5.1 -k test_cache_multimaster

# Краткий вывод (без print)
pytest api_tests/ --ip 192.168.5.1 --no-header -q
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

```text
I (XXXX) http_server: HTTP server started on port: 80
```

### 2. Запустить тесты в отдельном терминале

```bash
# Создать виртуальное окружение (если ещё нет)
python3 -m venv .venv
.venv/bin/pip install -r api_tests/requirements.txt

# Запустить все тесты
.venv/bin/pytest api_tests/ --ip localhost:8080
```

### Особенности QEMU-прогона

| Тест | Поведение в QEMU |
| ---- | ---------------- |
| `test_wifi_scanner` | Сразу завершается с 2 фиктивными сетями |
| `test_cache_multimaster` | Переключает порт 1 в `cache_bus`, ждёт заполнения кэша (~2с), подключается к `localhost:50504` |
| `test_reboot_command` | Перезагружает QEMU-эмулятор, ждёт возврата |

---

## Добавление нового теста

1. Добавьте функцию в подходящий файл (или создайте новый `test_*.py`)
2. Используйте фикстуру `api` — она предоставляет авторизованный `WBMGEAPI` клиент
3. Задайте порядок через `@pytest.mark.order(N)`

```python
import pytest

@pytest.mark.order(31)
def test_new_feature(api):
    response = api.session.get(f"{api.base_url}/new_endpoint", timeout=10)
    assert response.status_code == 200
    data = response.json()
    assert "expected_field" in data
    print("✓ New feature works")
```
