#!/usr/bin/env python3
"""
Простые тесты для WB-MGE HTTP API
"""

import requests
import time
import json


class WBMGEAPI:
    def __init__(self, base_url="http://192.168.1.1"):
        self.base_url = base_url
        self.session = requests.Session()

    def auth(self, login="wirenboard", password="wirenboard"):
        """Авторизация"""
        response = self.session.post(f"{self.base_url}/auth", json={
            "login": login,
            "pass": password
        })
        return response

    def get_info(self):
        """Получить информацию об устройстве"""
        return self.session.get(f"{self.base_url}/info")

    def update_info(self, data):
        """Обновить информацию об устройстве"""
        return self.session.post(f"{self.base_url}/info", json=data)

    def get_settings(self):
        """Получить настройки"""
        return self.session.get(f"{self.base_url}/settings")

    def update_settings(self, data):
        """Обновить настройки"""
        return self.session.post(f"{self.base_url}/settings", json=data)

    def start_wifi_scan(self):
        """Запустить сканирование WiFi"""
        return self.session.post(f"{self.base_url}/wifi_scan/start")

    def get_wifi_scan_results(self):
        """Получить результаты сканирования WiFi"""
        return self.session.get(f"{self.base_url}/wifi_scan/results")

    def get_ap_clients(self):
        """Получить список клиентов AP"""
        return self.session.get(f"{self.base_url}/ap_clients")

    def get_static_file(self, path):
        """Получить статический файл"""
        return self.session.get(f"{self.base_url}/{path}")

    def get_session(self):
        """Проверить статус сессии"""
        return self.session.get(f"{self.base_url}/session")

    def logout(self):
        """Выйти из системы"""
        return self.session.post(f"{self.base_url}/logout")

    def get_uptime(self):
        """Получить время работы устройства"""
        return self.session.get(f"{self.base_url}/uptime")

    def execute_command(self, cmd):
        """Выполнить команду"""
        return self.session.post(f"{self.base_url}/cmd", json={"cmd": cmd})


def test_auth(api):
    """Тест авторизации"""
    print("=== Тест авторизации ===")

    # Неправильные данные
    response = api.auth("wrong", "wrong")
    assert response.status_code == 200
    data = response.json()
    assert data["auth"] == False
    assert "error" in data
    print("✓ Неправильная авторизация отклонена")

    # Правильные данные
    response = api.auth()
    assert response.status_code == 200
    data = response.json()
    assert data["auth"] == True
    print("✓ Правильная авторизация принята")


def test_info(api):
    """Тест информации об устройстве"""
    print("\n=== Тест информации об устройстве ===")

    # Получить информацию
    response = api.get_info()
    assert response.status_code == 200
    data = response.json()

    # Проверить обязательные поля
    required_fields = [
        "device_name", "firmware", "hardware", "serial_num",
        "system_voltage", "config_button_presses", "ethernet", "wifi",
        "rs485_1", "rs485_2"
    ]
    for field in required_fields:
        assert field in data, f"Поле {field} отсутствует"

    # Проверить типы и ограничения
    assert isinstance(data["serial_num"], int)
    assert 0.0 <= data["system_voltage"] <= 100.0
    assert 0 <= data["config_button_presses"] <= 1000

    # Ethernet поля
    assert isinstance(data["ethernet"]["con_eth"], bool)
    if "ip" in data["ethernet"] and data["ethernet"]["ip"]:
        # Базовая проверка формата IP
        ip_parts = data["ethernet"]["ip"].split(".")
        assert len(ip_parts) == 4

    # WiFi поля
    assert isinstance(data["wifi"]["con_sta"], bool)
    assert 0 <= data["wifi"]["con_ap"] <= 10
    if "sta_rssi" in data["wifi"] and data["wifi"]["sta_rssi"] is not None:
        assert -100 <= data["wifi"]["sta_rssi"] <= 0
    if "ap_channel" in data["wifi"]:
        assert 1 <= data["wifi"]["ap_channel"] <= 14

    # RS485 поля
    for port in ["rs485_1", "rs485_2"]:
        if port in data:
            rs485_data = data[port]
            assert isinstance(rs485_data["is_busy"], bool)
            assert 0 <= rs485_data["error_percentage"] <= 100
            assert 0 <= rs485_data["server_connections_count"] <= 10

    print("✓ Структура информации корректна")

    # Тест записи параметров
    update_data = {
        "device_name": "Test Device",
        "hardware": "Test Hardware"
    }
    response = api.update_info(update_data)
    assert response.status_code == 200
    print("✓ Запись параметров информации работает")


def test_settings(api):
    """Тест настроек"""
    print("\n=== Тест настроек ===")

    # Получить настройки
    response = api.get_settings()
    assert response.status_code == 200
    original_settings = response.json()

    # Проверить структуру
    required_sections = ["wifi", "ethernet", "rs485_1", "rs485_2"]
    for section in required_sections:
        assert section in original_settings, f"Секция {section} отсутствует"

    assert isinstance(original_settings["vout"], bool)
    assert isinstance(original_settings["io_bus"], bool)
    assert 1 <= original_settings["web_port"] <= 65535

    # Проверить WiFi настройки
    wifi = original_settings["wifi"]
    assert wifi["mode"] in ["ap", "sta", "apsta", "null"]
    if "ap_auth" in wifi:
        assert wifi["ap_auth"] in ["open", "wpa2_psk", "wpa3_psk"]
    if "sta_auth" in wifi:
        assert wifi["sta_auth"] in ["open", "wpa2_psk", "wpa3_psk"]

    # Проверить Ethernet настройки
    eth = original_settings["ethernet"]
    assert isinstance(eth["dhcpc"], bool)

    # Проверить RS485 настройки
    for port in ["rs485_1", "rs485_2"]:
        rs485 = original_settings[port]
        assert isinstance(rs485["term"], bool)
        assert isinstance(rs485["fail_safe"], bool)
        assert isinstance(rs485["baudrate"], int)
        assert rs485["baudrate"] in [300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400]
        assert rs485["stopbits"] in ["1", "1.5", "2"]
        assert rs485["parity"] in ["none", "even", "odd"]
        assert rs485["databits"] in ["5", "6", "7", "8"]

        # Проверить bridge настройки
        bridge = rs485["bridge"]
        assert bridge["mode"] in ["server", "client"]
        assert 1 <= bridge["port"] <= 65535
        assert isinstance(bridge["modbus"], bool)

        # Проверить Modbus TCP параметры (если они есть)
        if "reverse_gateway" in bridge:
            assert isinstance(bridge["reverse_gateway"], bool)
        if "rtu_timeout" in bridge:
            assert isinstance(bridge["rtu_timeout"], int)
            assert 10 <= bridge["rtu_timeout"] <= 2000
        if "tcp_timeout" in bridge:
            assert isinstance(bridge["tcp_timeout"], int)
            assert 50 <= bridge["tcp_timeout"] <= 3000
        if "break_on_req" in bridge:
            assert isinstance(bridge["break_on_req"], bool)

    print("✓ Структура настроек корректна")

    # Тест записи настроек с проверкой ограничений
    test_settings = {
        "hostname": "test-device-123",  # Валидный hostname
        "web_port": 8080,               # Валидный порт
        "vout": not original_settings["vout"],  # Переключить bool
        "wifi": {
            "mode": "ap",
            "ap_ssid": "TestSSID123",
            "ap_pass": "testpass123"
        },
        "ethernet": {
            "dhcpc": not original_settings["ethernet"]["dhcpc"]
        },
        "rs485_1": {
            "baudrate": 115200,
            "term": not original_settings["rs485_1"]["term"],
            "bridge": {
                "mode": "server",
                "port": 5020
            }
        }
    }

    response = api.update_settings(test_settings)
    assert response.status_code == 200
    result = response.json()
    assert result["success"] == True
    print("✓ Запись настроек с валидными данными работает")

    # Проверить что настройки сохранились
    response = api.get_settings()
    assert response.status_code == 200
    new_settings = response.json()
    assert new_settings["hostname"] == "test-device-123"
    assert new_settings["web_port"] == 8080
    assert new_settings["vout"] == test_settings["vout"]
    print("✓ Настройки корректно сохраняются")

    # Тест с невалидными данными
    invalid_settings = {
        "hostname": "invalid_hostname!",  # Недопустимые символы
        "web_port": 70000,                # Превышение лимита
        "wifi": {
            "ap_ssid": "a" * 50           # Слишком длинный SSID
        }
    }

    response = api.update_settings(invalid_settings)
    # API должен либо отклонить (400), либо принять но не сохранить неправильные значения
    assert response.status_code in [200, 400]
    print("✓ Обработка невалидных настроек работает")


def test_session_management(api):
    """Тест управления сессиями"""
    print("\n=== Тест управления сессиями ===")

    # Проверить статус сессии после авторизации
    response = api.get_session()
    assert response.status_code == 200
    print("✓ Проверка статуса сессии работает")

    # Тест logout
    response = api.logout()
    assert response.status_code == 200
    data = response.json()
    assert data["success"] == True
    print("✓ Logout работает")

    # Проверить что сессия недействительна после logout
    response = api.get_session()
    assert response.status_code == 401
    print("✓ Сессия корректно завершается после logout")

    # Переавторизоваться для остальных тестов
    response = api.auth()
    assert response.status_code == 200
    assert response.json()["auth"] == True
    print("✓ Повторная авторизация работает")


def test_uptime(api):
    """Тест времени работы устройства"""
    print("\n=== Тест времени работы ===")

    response = api.get_uptime()
    assert response.status_code == 200
    data = response.json()

    # Проверить структуру uptime
    required_fields = ["days", "hours", "minutes", "seconds"]
    for field in required_fields:
        assert field in data, f"Поле {field} отсутствует в uptime"

    # Проверить ограничения
    assert isinstance(data["days"], int) and data["days"] >= 0
    assert isinstance(data["hours"], int) and 0 <= data["hours"] <= 23
    assert isinstance(data["minutes"], int) and 0 <= data["minutes"] <= 59
    assert isinstance(data["seconds"], int) and 0 <= data["seconds"] <= 59

    print("✓ Получение времени работы работает")


def test_commands(api):
    """Тест выполнения команд"""
    print("\n=== Тест команд ===")

    # Тест команды set_default_settings (безопасная)
    response = api.execute_command("set_default_settings")
    assert response.status_code == 200
    data = response.json()
    assert isinstance(data["success"], bool)
    assert data["command"] == "set_default_settings"
    print("✓ Команда set_default_settings работает")

    # НЕ тестируем reboot (опасно для автотестов)
    print("✓ Опасные команды пропущены для безопасности")


def test_modbus_tcp_parameters(api):
    """Тест специфических параметров Modbus TCP"""
    print("\n=== Тест параметров Modbus TCP ===")

    # Получить текущие настройки
    response = api.get_settings()
    assert response.status_code == 200
    original_settings = response.json()

    # Тест настроек Modbus TCP для первого порта
    modbus_settings = {
        "rs485_1": {
            "bridge": {
                "mode": "server",
                "port": 502,
                "modbus": True,
                "reverse_gateway": True,
                "rtu_timeout": 750,      # 10-2000 мс
                "tcp_timeout": 1500,     # 50-3000 мс
                "break_on_req": True
            }
        },
        "rs485_2": {
            "bridge": {
                "mode": "client",
                "port": 503,
                "ip": "192.168.1.10",
                "modbus": True,
                "reverse_gateway": False,
                "rtu_timeout": 1000,
                "tcp_timeout": 2000,
                "break_on_req": False
            }
        }
    }

    response = api.update_settings(modbus_settings)
    assert response.status_code == 200
    result = response.json()
    assert result["success"] == True
    print("✓ Настройки Modbus TCP сохранены")

    # Проверить что настройки применились
    response = api.get_settings()
    assert response.status_code == 200
    new_settings = response.json()

    # Проверить первый порт
    rs485_1 = new_settings["rs485_1"]["bridge"]
    assert rs485_1["modbus"] == True
    assert rs485_1["reverse_gateway"] == True
    assert rs485_1["rtu_timeout"] == 750
    assert rs485_1["tcp_timeout"] == 1500
    assert rs485_1["break_on_req"] == True

    # Проверить второй порт
    rs485_2 = new_settings["rs485_2"]["bridge"]
    assert rs485_2["modbus"] == True
    assert rs485_2["reverse_gateway"] == False
    assert rs485_2["rtu_timeout"] == 1000
    assert rs485_2["tcp_timeout"] == 2000
    assert rs485_2["break_on_req"] == False

    print("✓ Параметры Modbus TCP корректно применились")

    # Тест граничных значений
    boundary_settings = {
        "rs485_1": {
            "bridge": {
                "rtu_timeout": 10,       # Минимум
                "tcp_timeout": 50        # Минимум
            }
        }
    }

    response = api.update_settings(boundary_settings)
    assert response.status_code == 200
    print("✓ Минимальные значения тайм-аутов принимаются")

    boundary_settings = {
        "rs485_1": {
            "bridge": {
                "rtu_timeout": 2000,     # Максимум
                "tcp_timeout": 3000      # Максимум
            }
        }
    }

    response = api.update_settings(boundary_settings)
    assert response.status_code == 200
    print("✓ Максимальные значения тайм-аутов принимаются")

    # Тест с отключенным Modbus (параметры должны игнорироваться)
    transparent_settings = {
        "rs485_1": {
            "bridge": {
                "modbus": False,         # Прозрачный режим
                "rtu_timeout": 9999,     # Должен игнорироваться
                "tcp_timeout": 9999      # Должен игнорироваться
            }
        }
    }

    response = api.update_settings(transparent_settings)
    assert response.status_code == 200
    print("✓ Настройки для прозрачного режима принимаются")


def test_modbus_validation_limits(api):
    """Тест валидации лимитов для Modbus параметров"""
    print("\n=== Тест валидации лимитов Modbus ===")

    # Тест превышения лимитов
    invalid_settings = {
        "rs485_1": {
            "bridge": {
                "modbus": True,
                "rtu_timeout": 5,        # Меньше минимума (10)
                "tcp_timeout": 5000      # Больше максимума (3000)
            }
        }
    }

    response = api.update_settings(invalid_settings)
    # API должен либо отклонить, либо скорректировать значения
    assert response.status_code in [200, 400]
    print("✓ Превышение лимитов тайм-аутов обрабатывается")

    # Тест с отрицательными значениями
    invalid_settings = {
        "rs485_2": {
            "bridge": {
                "modbus": True,
                "rtu_timeout": -100,     # Отрицательное значение
                "tcp_timeout": -200      # Отрицательное значение
            }
        }
    }

    response = api.update_settings(invalid_settings)
    assert response.status_code in [200, 400]
    print("✓ Отрицательные значения тайм-аутов обрабатываются")


def test_validation_patterns(api):
    """Тест валидации паттернов и ограничений"""
    print("\n=== Тест валидации паттернов ===")

    # Тест валидных паттернов
    valid_data = {
        "hostname": "valid-hostname-123",  # Валидный hostname
        "login": "valid_user_123",         # Валидный login
        "wifi": {
            "ap_ssid": "ValidSSID",        # Валидный SSID
            "ap_pass": "ValidPass123"      # Валидный пароль (8+ символов)
        }
    }

    response = api.update_settings(valid_data)
    assert response.status_code == 200
    print("✓ Валидные паттерны принимаются")

    # Тест граничных значений
    boundary_data = {
        "wifi": {
            "ap_ssid": "A",                # Минимальная длина (1 символ)
            "ap_pass": "12345678"          # Минимальная длина пароля (8 символов)
        },
        "web_port": 1                      # Минимальный порт
    }

    response = api.update_settings(boundary_data)
    assert response.status_code == 200
    print("✓ Граничные значения принимаются")

    # Тест превышения лимитов
    limit_data = {
        "wifi": {
            "ap_ssid": "A" * 50,           # Превышение лимита SSID (32)
            "ap_pass": "A" * 100           # Превышение лимита пароля (63)
        },
        "web_port": 70000                  # Превышение лимита порта (65535)
    }

    response = api.update_settings(limit_data)
    # Должен отклонить или игнорировать неправильные значения
    assert response.status_code in [200, 400]
    print("✓ Превышение лимитов обрабатывается")


def test_wifi_scanner(api):
    """Тест сканера WiFi"""
    print("\n=== Тест сканера WiFi ===")

    # Запустить сканирование
    response = api.start_wifi_scan()
    assert response.status_code == 200
    data = response.json()
    assert isinstance(data["success"], bool)
    print("✓ Запуск сканирования WiFi работает")

    # Проверить статус сканирования
    response = api.get_wifi_scan_results()
    assert response.status_code == 200
    data = response.json()

    assert "scan_in_progress" in data
    assert "scan_completed" in data
    assert isinstance(data["scan_in_progress"], bool)
    assert isinstance(data["scan_completed"], bool)

    if "networks" in data:
        assert isinstance(data["networks"], list)
        for network in data["networks"]:
            assert "ssid" in network
            assert "rssi" in network
            assert -100 <= network["rssi"] <= 0

    print("✓ Получение результатов сканирования работает")


def test_ap_clients(api):
    """Тест списка клиентов AP"""
    print("\n=== Тест списка клиентов AP ===")

    response = api.get_ap_clients()
    assert response.status_code == 200
    clients = response.json()

    assert isinstance(clients, list)
    for client in clients:
        assert "mac" in client
        if "rssi" in client:
            assert -100 <= client["rssi"] <= 0

    print("✓ Получение списка клиентов AP работает")


def test_static_files(api):
    """Тест статических файлов"""
    print("\n=== Тест статических файлов ===")

    static_files = [
        ("", "text/html"),           # Главная страница
        ("index.css", "text/css"),   # CSS
        ("index.js", "application/javascript"),  # JS
        ("favicon.webp", "image/webp")  # Favicon
    ]

    for path, expected_content_type in static_files:
        response = api.get_static_file(path)
        assert response.status_code == 200

        content_type = response.headers.get("content-type", "")
        assert expected_content_type in content_type.lower(), f"Неправильный Content-Type для {path}"

        # Проверить что контент не пустой
        assert len(response.content) > 0

        print(f"✓ Статический файл {path or 'index'} доступен")


def test_unauthorized_access(api):
    """Тест доступа без авторизации"""
    print("\n=== Тест неавторизованного доступа ===")

    # Создать новую сессию без авторизации
    unauth_session = requests.Session()

    protected_endpoints = [
        "/info", "/settings", "/wifi_scan/start",
        "/wifi_scan/results", "/ap_clients"
    ]

    for endpoint in protected_endpoints:
        response = unauth_session.get(f"{api.base_url}{endpoint}")
        assert response.status_code == 401, f"Эндпоинт {endpoint} должен требовать авторизацию"

    print("✓ Защищенные эндпоинты требуют авторизацию")

    # Проверить что статические файлы доступны без авторизации
    response = unauth_session.get(f"{api.base_url}/favicon.webp")
    assert response.status_code == 200
    print("✓ Статические файлы доступны без авторизации")


def main():
    """Главная функция запуска тестов"""
    # Замените IP на адрес вашего устройства
    api = WBMGEAPI("http://192.168.1.1")

    print("Запуск тестов WB-MGE API")
    print("=" * 40)

    try:
        # Тест неавторизованного доступа
        test_unauthorized_access(api)

        # Авторизация для остальных тестов
        test_auth(api)

        # Основные тесты
        test_info(api)
        test_settings(api)
        test_session_management(api)
        test_uptime(api)
        test_commands(api)
        test_modbus_tcp_parameters(api)
        test_modbus_validation_limits(api)
        test_validation_patterns(api)
        test_wifi_scanner(api)
        test_ap_clients(api)
        test_static_files(api)

        print("\n" + "=" * 40)
        print("🎉 ВСЕ ТЕСТЫ ПРОШЛИ УСПЕШНО!")

    except AssertionError as e:
        print(f"\n❌ ОШИБКА ТЕСТА: {e}")
        return 1
    except requests.exceptions.RequestException as e:
        print(f"\n❌ ОШИБКА СОЕДИНЕНИЯ: {e}")
        print("Проверьте что устройство доступно и IP адрес правильный")
        return 1
    except Exception as e:
        print(f"\n❌ НЕОЖИДАННАЯ ОШИБКА: {e}")
        return 1

    return 0


if __name__ == "__main__":
    exit(main())