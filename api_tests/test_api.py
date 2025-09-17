#!/usr/bin/env python3
"""
Простые тесты для WB-MGE HTTP API
"""

import requests
import time
import json


class WBMGEAPI:
    def __init__(self, base_url="http://192.168.5.1"):
        self.base_url = base_url
        self.session = requests.Session()

        # Set headers to mimic a regular browser
        self.session.headers.update({
            'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
            'Accept': 'application/json, text/plain, */*',
            'Accept-Language': 'en-US,en;q=0.9',
            'Accept-Encoding': 'identity',  # Избегаем сжатия
            'Connection': 'close',          # Закрываем соединение после каждого запроса
            'Cache-Control': 'no-cache',
        })

        # Disable SSL verification
        self.session.verify = False

        # Suppress SSL warnings
        try:
            import urllib3
            urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
        except ImportError:
            pass  # urllib3 not available

    def auth(self, login="admin", password="admin"):
        """Авторизация"""
        try:
            response = self.session.post(f"{self.base_url}/auth", json={
                "login": login,
                "pass": password
            }, timeout=10)
            return response
        except requests.exceptions.RequestException:
            raise

    def get_info(self):
        """Получить информацию об устройстве"""
        return self.session.get(f"{self.base_url}/info", timeout=10)

    def get_settings(self):
        """Получить настройки"""
        return self.session.get(f"{self.base_url}/settings", timeout=10)

    def update_settings(self, data):
        """Обновить настройки"""
        return self.session.post(f"{self.base_url}/settings", json=data, timeout=10)

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
        try:
            print(f"Отправка команды: {cmd}")
            payload = {"cmd": cmd}
            print(f"JSON payload: {payload}")

            response = self.session.post(f"{self.base_url}/cmd", json=payload, timeout=10)
            print(f"Команда {cmd} отправлена, статус: {response.status_code}")

            return response
        except requests.exceptions.RequestException as e:
            print(f"Ошибка при отправке команды {cmd}: {e}")
            raise


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

    # Проверить обязательные поля согласно новой структуре API
    required_fields = [
        "device_name", "signature", "firmware", "git_info",
        "serial_num", "system_voltage", "config_button_presses"
    ]
    for field in required_fields:
        assert field in data, f"Поле {field} отсутствует"

    # Проверить типы данных основных полей
    assert isinstance(data["serial_num"], int), "Поле serial_num имеет неверный тип"
    assert isinstance(data["system_voltage"], (int, float)), "Поле system_voltage имеет неверный тип"
    assert isinstance(data["config_button_presses"], int), "Поле config_button_presses имеет неверный тип"

    # Проверить структуру ethernet
    assert "ethernet" in data, "Секция ethernet отсутствует"
    eth = data["ethernet"]

    # Проверить поля структуры ethernet
    ethernet_fields = [
        "con_eth", "ip", "mask", "gw", "mac"
    ]
    for field in ethernet_fields:
        assert field in eth, f"Поле {field} отсутствует"

    assert isinstance(eth["con_eth"], bool), "Поле con_eth имеет неверный тип"

    # Проверить структуру wifi
    assert "wifi" in data, "Секция wifi отсутствует"
    wifi = data["wifi"]

    # Проверить поля структуры wifi
    wifi_fields = [
        "enabled", "mode", "con_sta", "con_sta_ssid", "sta_ip", "sta_mask", "sta_gw",
        "con_ap", "ap_ip", "ap_mask", "ap_gw", "sta_rssi", "ap_channel", "sta_mac", "ap_mac"
    ]
    for field in wifi_fields:
        assert field in wifi, f"Поле {field} отсутствует"

    assert isinstance(wifi["enabled"], bool), "Поле enabled имеет неверный тип"
    assert isinstance(wifi["con_sta"], bool), "Поле con_sta имеет неверный тип"
    assert isinstance(wifi["con_ap"], int), "Поле con_ap имеет неверный тип"
    assert isinstance(wifi["sta_rssi"], int), "Поле sta_rssi имеет неверный тип"
    assert isinstance(wifi["ap_channel"], int), "Поле ap_channel имеет неверный тип"

    assert 0 <= wifi["con_ap"] <= 10, f"Поле con_ap имеет неверное значение: {wifi['con_ap']}"
    assert -128 <= wifi["sta_rssi"] <= 127, f"Поле sta_rssi имеет неверное значение: {wifi['sta_rssi']}"
    assert 1 <= wifi["ap_channel"] <= 13, f"Поле ap_channel имеет неверное значение: {wifi['ap_channel']}"

    # Проверить структуру rs485 портов
    for port in ["rs485_1", "rs485_2"]:
        assert port in data, f"Секция {port} отсутствует"
        rs485 = data[port]

        assert "is_busy" in rs485, "Поле is_busy отсутствует"
        assert "error_percentage" in rs485, "Поле error_percentage отсутствует"
        assert "server_connections_count" in rs485, "Поле server_connections_count отсутствует"

        assert isinstance(rs485["is_busy"], bool), "Поле is_busy имеет неверный тип"
        assert isinstance(rs485["error_percentage"], int), "Поле error_percentage имеет неверный тип"
        assert isinstance(rs485["server_connections_count"], int), "Поле server_connections_count имеет неверный тип"

    print("✓ Структура информации корректна")


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

    assert "hostname" in original_settings, "Поле vout отсутствует"
    assert "login" in original_settings, "Поле vout отсутствует"
    assert "pass" in original_settings, "Поле vout отсутствует"
    assert "vout" in original_settings, "Поле vout отсутствует"
    assert "web_port" in original_settings, "Поле web_port отсутствует"
    assert "io_bus" in original_settings, "Поле io_bus отсутствует"

    assert isinstance(original_settings["vout"], bool), "Поле vout имеет неверный тип"
    assert isinstance(original_settings["web_port"], int), "Поле web_port имеет неверный тип"
    assert 1 <= original_settings["web_port"] <= 65535, f"Поле web_port имеет неверное значение: {original_settings['web_port']}"
    assert isinstance(original_settings["io_bus"], bool), "Поле io_bus имеет неверный тип"

    # Проверить WiFi настройки
    wifi = original_settings["wifi"]
    wifi_fields = [
        "mode", "ap_auth", "sta_auth", "ap_ssid", "ap_pass", "sta_ssid", "sta_pass",
        "ap_ip_static", "ap_mask_static", "ap_gw_static",
        "sta_dhcpc", "sta_ip_static", "sta_mask_static", "sta_gw_static"
    ]
    for field in wifi_fields:
        assert field in wifi, f"Поле {field} отсутствует"

    assert isinstance(wifi["sta_dhcpc"], bool), "Поле sta_dhcpc имеет неверный тип"

    assert wifi["mode"] in ["ap", "sta", "apsta", "none"], f"Поле mode имеет неверное значение: {wifi['mode']}"
    assert wifi["ap_auth"] in ["open", "wpa2_psk", "wpa3_psk"], f"Поле ap_auth имеет неверное значение: {wifi['ap_auth']}"
    assert wifi["sta_auth"] in ["open", "wpa2_psk", "wpa3_psk"], f"Поле sta_auth имеет неверное значение: {wifi['sta_auth']}"

    # Проверить Ethernet настройки
    eth = original_settings["ethernet"]
    eth_fields = [
        "ip_static", "mask_static", "gw_static", "dhcpc"
    ]
    for field in eth_fields:
        assert field in eth, f"Поле {field} отсутствует"

    assert isinstance(eth["dhcpc"], bool), "Поле dhcpc имеет неверный тип"

    # Проверить RS485 настройки
    for port in ["rs485_1", "rs485_2"]:
        rs485 = original_settings[port]
        rs485_fields = [
            "term", "fail_safe", "baudrate", "stopbits",
            "parity", "databits", "bridge"
        ]
        for field in rs485_fields:
            assert field in rs485, f"Поле {field} отсутствует"

        assert isinstance(rs485["term"], bool), f"Поле term имеет неверный тип"
        assert isinstance(rs485["fail_safe"], bool), f"Поле fail_safe имеет неверный тип"
        assert isinstance(rs485["baudrate"], int), f"Поле baudrate имеет неверный тип"
        assert rs485["baudrate"] in [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200], \
            f"Поле baudrate имеет неверное значение: {rs485['baudrate']}"
        assert rs485["stopbits"] in ["1", "1.5", "2"], \
            f"Поле stopbits имеет неверное значение: {rs485['stopbits']}"
        assert rs485["parity"] in ["none", "even", "odd"], \
            f"Поле parity имеет неверное значение: {rs485['parity']}"
        assert rs485["databits"] in ["5", "6", "7", "8"], \
            f"Поле databits имеет неверное значение: {rs485['databits']}"

        # Проверить bridge настройки
        bridge = rs485["bridge"]
        bridge_fields = [
            "mode", "port", "ip", "modbus"
        ]
        for field in bridge_fields:
            assert field in bridge, f"Поле {field} отсутствует"

        assert bridge["mode"] in ["server", "client"], f"Поле mode имеет неверное значение: {bridge['mode']}"
        assert isinstance(bridge["port"], int), "Поле port имеет неверный тип"
        assert 1 <= bridge["port"] <= 65535, f"Поле port имеет неверное значение: {bridge['port']}"
        assert isinstance(bridge["modbus"], bool), "Поле modbus имеет неверный тип"

        # Modbus TCP specific parameters removed from test

    print("✓ Структура настроек корректна")

    # Тест записи настроек с проверкой ограничений
    test_settings = {
        "hostname": "test-device-123",  # Валидный hostname
        #"login": "testuser123",         # Валидный login # NOTE: Пока не трогаем, иначе дальше ломается авторизация
        "web_port": 8080,               # Валидный порт
        "vout": not original_settings["vout"],  # Переключить bool
        "io_bus": not original_settings["io_bus"],  # Переключить bool
        "wifi": {
            "mode": "sta",
            "ap_auth": "wpa2_psk",
            "sta_auth": "wpa3_psk",
            "ap_ssid": "Test-SSID#123.",
            "ap_pass": "testpass123#*~",
            "sta_ssid": "Station-SSID",
            "sta_pass": "#stapass.456",
            "ap_ip_static": "192.168.4.1",
            "ap_mask_static": "255.255.255.0",
            "ap_gw_static": "192.168.4.1",
            "sta_dhcpc": not original_settings["wifi"]["sta_dhcpc"],
            "sta_ip_static": "192.168.2.7",
            "sta_mask_static": "255.255.255.0",
            "sta_gw_static": "192.168.2.1"
        },
        "ethernet": {
            "dhcpc": not original_settings["ethernet"]["dhcpc"],
            "ip_static": "192.168.1.100",
            "mask_static": "255.255.255.0",
            "gw_static": "192.168.1.1"
        },
        "rs485_1": {
            "term": not original_settings["rs485_1"]["term"],
            "fail_safe": not original_settings["rs485_1"]["fail_safe"],
            "baudrate": 115200,
            "stopbits": "1.5",
            "parity": "even",
            "databits": "7",
            "bridge": {
                "mode": "server",
                "port": 5020,
                "ip": "192.168.1.49",
                "modbus": True
            }
        },
        "rs485_2": {
            "term": not original_settings["rs485_2"]["term"],
            "fail_safe": not original_settings["rs485_2"]["fail_safe"],
            "baudrate": 38400,
            "stopbits": "1",
            "parity": "odd",
            "databits": "6",
            "bridge": {
                "mode": "client",
                "port": 5021,
                "ip": "192.168.1.50",
                "modbus": False
            }
        }
    }

    response = api.update_settings(test_settings)
    assert response.status_code == 200
    result = response.json()
    assert result["success"] == True
    print("✓ Запись настроек с валидными данными работает")

    # Проверить что все настройки сохранились
    response = api.get_settings()
    assert response.status_code == 200
    new_settings = response.json()

    # Проверить основные параметры
    main_fields = [
        "hostname", "vout", "web_port", "io_bus"
        # "login", "pass"
    ]
    for field in main_fields:
        assert new_settings[field] == test_settings[field], f"Неверное значение поля {field}: {new_settings[field]}"

    # Проверить WiFi настройки
    wifi = new_settings["wifi"]
    for field in wifi_fields:
        assert wifi[field] == test_settings["wifi"][field], f"Неверное значение поля {field}: {wifi[field]}"

    # Проверить Ethernet настройки
    eth = new_settings["ethernet"]
    for field in eth_fields:
        assert eth[field] == test_settings["ethernet"][field], f"Неверное значение поля {field}: {eth[field]}"

    # Проверить RS485_1 настройки
    rs485_1 = new_settings["rs485_1"]
    rs485_main_fields = [
        "term", "fail_safe", "baudrate", "stopbits", "parity", "databits"
    ]
    for field in rs485_main_fields:
        assert rs485_1[field] == test_settings["rs485_1"][field], \
            f"Неверное значение поля {field}: {rs485_1[field]}"

    bridge_1 = new_settings["rs485_1"]["bridge"]
    bridge_fields = [
        "mode", "port", "ip", "modbus"
    ]
    for field in bridge_fields:
        assert bridge_1[field] == test_settings["rs485_1"]["bridge"][field], \
            f"Неверное значение поля {field}: {bridge_1[field]}"

    # Проверить RS485_2 настройки
    rs485_2 = new_settings["rs485_2"]
    for field in rs485_main_fields:
        assert rs485_2[field] == test_settings["rs485_2"][field], \
            f"Неверное значение поля {field}: {rs485_2[field]}"

    bridge_2 = new_settings["rs485_2"]["bridge"]
    for field in bridge_fields:
        assert bridge_2[field] == test_settings["rs485_2"]["bridge"][field], \
            f"Неверное значение поля {field}: {bridge_2[field]}"

    print("✓ Все настройки корректно сохраняются")

    # Тест с невалидными данными
    invalid_settings = {
        "hostname": "invalid_hostname!",            # Недопустимые символы
        "web_port": 70000,                          # Превышение лимита
        "wifi": {
            "mode": "disabled",                     # Отсутствующий режим
            "ap_auth": "close",                     # Отсутствующий режим
            "sta_auth": "wep",                      # Отсутствующий режим
            "ap_ssid": "a" * 50,                    # Слишком длинный SSID
            "sta_ssid": "фыва123",                  # Недопустимые символы
            "ap_ip_static": "123.456.789.101",      # Недопустимые значения байтов
            "ap_mask_static": "abc.def.ghi.jkl",    # Недопустимые символы
            "ap_gw_static": True,                   # Неверный тип
            "sta_ip_static": "192.168.1.1.1",       # Неверный формат
            "sta_mask_static": "123.aaa.1.1",       # Недопустимые символы
            "sta_gw_static": 192                    # Неверный тип
        },
        "ethernet": {
            "ip_static": "123.456.789.101",         # Недопустимые значения байтов
            "mask_static": 456,                     # Неверный тип
            "gw_static": 789,                       # Неверный тип
            "dhcpc": 0                              # Неверный тип
        },
        "rs485_1": {
            "term": 1,                              # Неверный тип
            "fail_safe": "off",                     # Неверный тип
            "baudrate": 123456,                     # Недопустимое значение
            "stopbits": "2.5",                      # Недопустимое значение
            "parity": "all",                        # Недопустимое значение
            "databits": "2",                        # Недопустимое значение
            "bridge": {
                "mode": "station",                  # Недопустимое значение
                "port": 0,                          # Недопустимое значение
                "ip": "201.250.252.256",            # Недопустимые значения байтов
                "modbus": "enabled"                 # Неверный тип
            },
        "rs485_2": {
            "term": "true",                         # Неверный тип
            "fail_safe": "true",                    # Неверный тип
            "baudrate": 0,                          # Недопустимое значение
            "stopbits": "0.5",                      # Недопустимое значение
            "parity": "disabled",                   # Недопустимое значение
            "databits": "4",                        # Недопустимое значение
            "bridge": {
                "mode": "server",                   # Недопустимое значение
                "port": 65536,                      # Недопустимое значение
                "ip": "102.abc.126.18",             # Недопустимые символы
                "modbus": "disabled"                # Неверный тип
            }
        },
        "vout": "true",                             # Неверный тип
        "io_bus": "true"                            # Неверный тип
        }
    }

    response = api.update_settings(invalid_settings)
    # API должен либо отклонить (400), либо принять но не сохранить неправильные значения
    assert response.status_code in [200, 400]
    print("✓ Обработка невалидных настроек работает")

    # Проверка, что не сохраняются невалидные настройки
    response = api.get_settings()
    assert response.status_code == 200
    valid_settings = response.json()
    assert valid_settings == new_settings, "Были сохранены невалидные настройки"
    print("✓ Невалидные настройки не сохраняются")

    # Откат настроек
    response = api.update_settings(original_settings)
    assert response.status_code == 200
    print("✓ Возвращены исходные настройки")


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
    assert data["logout"] == True  # API возвращает "logout", не "success"
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

    try:
        # Тест команды set_default_settings (безопасная)
        print("Отправка команды set_default_settings...")
        response = api.execute_command("set_default_settings")

        print(f"Status Code: {response.status_code}")
        print(f"Headers: {response.headers}")
        print(f"Content: {response.text[:500]}...")  # Первые 500 символов

        assert response.status_code == 200, f"Ожидался статус 200, получен {response.status_code}"

        # По коду HTTP сервера, команды возвращают пустой ответ, а не JSON
        if response.text.strip():
            try:
                data = response.json()
                print(f"JSON Response: {data}")
            except Exception as e:
                print(f"Не удалось разобрать JSON: {e}")
                print(f"Raw response: {response.text}")
        else:
            print("Получен пустой ответ (ожидается для команд)")

        print("✓ Команда set_default_settings работает")

        # НЕ тестируем reboot (опасно для автотестов)
        print("✓ Опасные команды пропущены для безопасности")

    except requests.exceptions.RequestException as e:
        print(f"❌ Ошибка соединения при выполнении команды: {e}")
        raise
    except Exception as e:
        print(f"❌ Неожиданная ошибка в тесте команд: {e}")
        print(f"Тип ошибки: {type(e).__name__}")
        raise


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
                "modbus": True
            }
        },
        "rs485_2": {
            "bridge": {
                "mode": "client",
                "port": 503,
                "ip": "192.168.1.10",
                "modbus": True
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

    # Проверить второй порт
    rs485_2 = new_settings["rs485_2"]["bridge"]
    assert rs485_2["modbus"] == True

    print("✓ Параметры Modbus TCP корректно применились")

    # Тест с отключенным Modbus
    transparent_settings = {
        "rs485_1": {
            "bridge": {
                "modbus": False         # Прозрачный режим
            }
        }
    }

    response = api.update_settings(transparent_settings)
    assert response.status_code == 200
    print("✓ Настройки для прозрачного режима принимаются")


def test_modbus_validation_limits(api):
    """Тест валидации лимитов для Modbus параметров"""
    print("\n=== Тест валидации лимитов Modbus ===")

    # Тест с невалидными портами
    invalid_settings = {
        "rs485_1": {
            "bridge": {
                "modbus": True,
                "port": 0          # Невалидный порт
            }
        }
    }

    response = api.update_settings(invalid_settings)
    # API должен либо отклонить, либо скорректировать значения
    assert response.status_code in [200, 400]
    print("✓ Невалидные порты обрабатываются")

    # Тест с превышением лимита порта
    invalid_settings = {
        "rs485_2": {
            "bridge": {
                "modbus": True,
                "port": 70000      # Больше максимума (65535)
            }
        }
    }

    response = api.update_settings(invalid_settings)
    assert response.status_code in [200, 400]
    print("✓ Превышение лимитов портов обрабатывается")


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
    assert data["scan_in_progress"] == True, "scan_in_progress должен быть true"
    assert data["scan_completed"] == False, "scan_in_progress должен быть false"

    # Ожидание окончания сканирования
    timeout = 0
    while True:
        time.sleep(1)
        response = api.get_wifi_scan_results()
        assert response.status_code == 200
        data = response.json()
        if data["scan_in_progress"] == False and data["scan_completed"] == True:
            break
        timeout = timeout + 1
        assert timeout < 10, "Превышено время ожидания окончания сканирования"

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
        assert expected_content_type in content_type.lower(), f"Неправильный Content-Type для {path}: ожидался '{expected_content_type}', получен '{content_type}'"

        # Проверить что контент не пустой
        assert len(response.content) > 0

        print(f"✓ Статический файл {path or 'index'} доступен")


def test_unauthorized_access(api):
    """Тест доступа без авторизации"""
    print("\n=== Тест неавторизованного доступа ===")

    # Создать новую сессию без авторизации
    unauth_session = requests.Session()

    protected_endpoints = [
        ("/info", "GET"), ("/settings", "GET"), ("/wifi_scan/start", "POST"),
        ("/wifi_scan/results", "GET"), ("/ap_clients", "GET"), ("/uptime", "GET"),
        ("/session", "GET"), ("/update", "POST")
    ]

    for endpoint, method in protected_endpoints:
        if method == "GET":
            response = unauth_session.get(f"{api.base_url}{endpoint}")
        elif method == "POST":
            response = unauth_session.post(f"{api.base_url}{endpoint}")

        print(f"Тестируем {method} {endpoint}:")
        print(f"  Status Code: {response.status_code}")
        print(f"  Headers: {dict(response.headers)}")
        print(f"  Content: {response.text[:200]}...")

        assert response.status_code == 401, f"Эндпоинт {method} {endpoint} должен требовать авторизацию. Получен статус: {response.status_code}, содержимое: {response.text[:100]}"

    print("✓ Защищенные эндпоинты требуют авторизацию")

    # Проверить что статические файлы доступны без авторизации
    static_endpoints = ["/", "/index.css", "/index.js", "/favicon.webp"]

    for endpoint in static_endpoints:
        response = unauth_session.get(f"{api.base_url}{endpoint}")
        print(f"Тестируем GET {endpoint}:")
        print(f"  Status Code: {response.status_code}")
        assert response.status_code == 200

    print("✓ Статические файлы доступны без авторизации")


def quick_connection_test(base_url):
    """Быстрая проверка подключения перед запуском тестов"""
    import socket
    from urllib.parse import urlparse

    print("🔍 Быстрая проверка подключения...")

    parsed = urlparse(base_url)
    host = parsed.hostname or "192.168.5.1"
    port = parsed.port or 80

    try:
        # Проверка TCP порта
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        result = sock.connect_ex((host, port))
        sock.close()

        if result == 0:
            print(f"✅ TCP подключение к {host}:{port} успешно")

            # Дополнительно проверим HTTP запрос
            try:
                import requests
                response = requests.get(base_url + "/favicon.webp", timeout=5,
                                      headers={'Connection': 'close'})
                print(f"✅ HTTP тест успешен (Status: {response.status_code})")
                return True
            except Exception as e:
                print(f"⚠️  TCP работает, но HTTP провалился: {e}")
                print(f"💡 Возможно проблема в HTTP заголовках или протоколе")
                return False
        else:
            print(f"❌ TCP подключение к {host}:{port} провалилось")
            print(f"💡 Запустите диагностику: python diagnose_connection.py {base_url}")
            return False

    except Exception as e:
        print(f"❌ Ошибка проверки подключения: {e}")
        print(f"💡 Запустите диагностику: python diagnose_connection.py {base_url}")
        return False


def main():
    """Главная функция запуска тестов"""
    import argparse
    import sys

    # Парсинг аргументов командной строки
    parser = argparse.ArgumentParser(description='WB-MGE API Tests')
    parser.add_argument('--ip', default='192.168.5.1', help='IP address of WB-MGE device')
    parser.add_argument('--stop-on-failure', action='store_true', help='Stop on first test failure')
    parser.add_argument('--verbose', action='store_true', help='Verbose output')

    args = parser.parse_args()

    # Проверяем аргументы командной строки
    stop_on_failure = args.stop_on_failure or "--stop-on-failure" in sys.argv
    verbose = args.verbose or "--verbose" in sys.argv

    # Создаем API клиент с указанным IP
    api = WBMGEAPI(f"http://{args.ip}")

    print("Запуск тестов WB-MGE API")
    print("=" * 40)

    # Быстрая проверка подключения
    if not quick_connection_test(api.base_url):
        print("\n❌ Предварительная проверка подключения провалилась")
        print("🔧 Проверьте сетевое подключение перед запуском тестов")
        return 1

    if stop_on_failure:
        print("⚠️  Режим: остановка на первой ошибке")
    else:
        print("🔄 Режим: продолжение выполнения при ошибках")

    # Список всех тестов для выполнения
    tests = [
        ("неавторизованного доступа", test_unauthorized_access),
        ("авторизации", test_auth),
        ("информации об устройстве", test_info),
        ("настроек", test_settings),
        ("управления сессиями", test_session_management),
        ("времени работы", test_uptime),
        ("параметров Modbus TCP", test_modbus_tcp_parameters),
        ("валидации лимитов Modbus", test_modbus_validation_limits),
        ("валидации паттернов", test_validation_patterns),
        ("сканера WiFi", test_wifi_scanner),
        ("списка клиентов AP", test_ap_clients),
        ("статических файлов", test_static_files),
        ("команд", test_commands),
    ]

    passed = 0
    failed = 0
    failed_tests = []
    skipped = 0

    for test_name, test_func in tests:
        try:
            if not verbose:
                print(f"\n--- Выполняется тест {test_name} ---")
            else:
                print(f"\n🔍 Запуск теста: {test_name}")

            test_func(api)
            passed += 1
            print(f"✅ Тест {test_name} ПРОЙДЕН")

        except AssertionError as e:
            failed += 1
            error_msg = f"Ошибка теста: {e}"
            failed_tests.append((test_name, error_msg))
            print(f"❌ Тест {test_name} ПРОВАЛЕН: {error_msg}")

            if stop_on_failure:
                print(f"\n🛑 Остановка тестирования на первой ошибке")
                skipped = len(tests) - (passed + failed)
                break

        except requests.exceptions.RequestException as e:
            failed += 1
            error_msg = f"Ошибка соединения: {e}"
            failed_tests.append((test_name, error_msg))
            print(f"❌ Тест {test_name} ПРОВАЛЕН: {error_msg}")

            if stop_on_failure:
                print(f"\n🛑 Остановка тестирования на первой ошибке")
                skipped = len(tests) - (passed + failed)
                break

        except Exception as e:
            failed += 1
            error_msg = f"Неожиданная ошибка: {e}"
            failed_tests.append((test_name, error_msg))
            print(f"❌ Тест {test_name} ПРОВАЛЕН: {error_msg}")

            if stop_on_failure:
                print(f"\n🛑 Остановка тестирования на первой ошибке")
                skipped = len(tests) - (passed + failed)
                break

    # Итоговый отчет
    print("\n" + "=" * 60)
    print("ИТОГИ ТЕСТИРОВАНИЯ:")
    print(f"✅ Пройдено: {passed}")
    print(f"❌ Провалено: {failed}")
    if skipped > 0:
        print(f"⏸️  Пропущено: {skipped}")
    print(f"📊 Всего: {len(tests)}")

    if failed > 0:
        print("\n❌ ПРОВАЛИВШИЕСЯ ТЕСТЫ:")
        for test_name, error in failed_tests:
            print(f"  • {test_name}: {error}")

    if failed == 0:
        print("\n🎉 ВСЕ ТЕСТЫ ПРОШЛИ УСПЕШНО!")
        return 0
    else:
        success_rate = (passed / (passed + failed)) * 100
        print(f"\n⚠️  {failed} из {passed + failed} тестов провалились ({success_rate:.1f}% успешных)")

        if skipped == 0:
            print("\n💡 Используйте --stop-on-failure для остановки на первой ошибке")
            print("💡 Используйте --verbose для подробного вывода")

        return 1


if __name__ == "__main__":
    exit(main())