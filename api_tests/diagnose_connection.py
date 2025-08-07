#!/usr/bin/env python3
"""
Диагностика сетевого подключения к WB-MGE устройству
"""

import socket
import subprocess
import sys
import platform
from urllib.parse import urlparse

def ping_host(host, timeout=5):
    """Проверить доступность хоста через ping"""
    try:
        param = '-n' if platform.system().lower() == 'windows' else '-c'
        cmd = ['ping', param, '1', '-W' if platform.system().lower() == 'windows' else '-W', str(timeout * 1000), host]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 2)
        return result.returncode == 0
    except Exception as e:
        print(f"Ошибка ping: {e}")
        return False

def check_tcp_port(host, port, timeout=5):
    """Проверить TCP порт"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(timeout)
        result = sock.connect_ex((host, port))
        sock.close()
        return result == 0
    except Exception as e:
        print(f"Ошибка проверки порта {port}: {e}")
        return False

def test_http_connection(url):
    """Простой тест HTTP соединения"""
    try:
        import requests
        response = requests.get(url, timeout=10, verify=False)
        return True, response.status_code, len(response.content)
    except ImportError:
        return False, "requests не установлен", 0
    except requests.exceptions.ConnectionError as e:
        return False, f"Connection Error: {e}", 0
    except requests.exceptions.Timeout:
        return False, "Timeout", 0
    except Exception as e:
        return False, f"Other error: {e}", 0

def diagnose_connection(url="http://192.168.4.1"):
    """Диагностика подключения к устройству"""
    print("🔍 ДИАГНОСТИКА ПОДКЛЮЧЕНИЯ К WB-MGE")
    print("=" * 50)

    # Парсинг URL
    parsed = urlparse(url)
    host = parsed.hostname or "192.168.4.1"
    port = parsed.port or 80

    print(f"🎯 Цель: {host}:{port}")
    print()

    # 1. Проверка ping
    print("1️⃣ Проверка доступности хоста (ping)...")
    if ping_host(host):
        print(f"   ✅ {host} отвечает на ping")
    else:
        print(f"   ❌ {host} не отвечает на ping")
        print("   💡 Возможно устройство не подключено или ping заблокирован")
    print()

    # 2. Проверка TCP порта
    print(f"2️⃣ Проверка TCP порта {port}...")
    if check_tcp_port(host, port):
        print(f"   ✅ Порт {port} открыт")
    else:
        print(f"   ❌ Порт {port} закрыт или недоступен")
        print("   💡 Веб-сервер может быть выключен или слушать другой порт")
    print()

    # 3. Проверка HTTP соединения
    print("3️⃣ Проверка HTTP соединения...")
    success, status, content_length = test_http_connection(url)

    if success:
        print(f"   ✅ HTTP запрос успешен")
        print(f"   📊 Status Code: {status}")
        print(f"   📦 Content Length: {content_length} bytes")
    else:
        print(f"   ❌ HTTP запрос провален: {status}")
    print()

    # 4. Сетевая информация
    print("4️⃣ Информация о сети...")
    try:
        import netifaces
        gateways = netifaces.gateways()
        default_gateway = gateways.get('default', {}).get(netifaces.AF_INET, [None])[0]
        if default_gateway:
            print(f"   🌐 Шлюз по умолчанию: {default_gateway}")
        else:
            print("   ❓ Шлюз по умолчанию не найден")
    except ImportError:
        print("   ❓ netifaces не установлен (pip install netifaces для детальной диагностики)")

    # Проверим IP интерфейсы
    try:
        hostname = socket.gethostname()
        local_ip = socket.gethostbyname(hostname)
        print(f"   💻 Локальный IP: {local_ip}")
    except:
        print("   ❓ Не удалось определить локальный IP")
    print()

    # 5. Рекомендации
    print("5️⃣ Рекомендации:")

    if not ping_host(host):
        print("   🔧 Проверьте физическое подключение к сети")
        print("   🔧 Убедитесь что устройство включено")
        print("   🔧 Проверьте настройки WiFi/Ethernet")

        # Предложить альтернативные IP
        common_ips = ["192.168.1.1", "192.168.0.1", "10.0.0.1"]
        print("   🔧 Попробуйте альтернативные IP адреса:")
        for ip in common_ips:
            if ping_host(ip, timeout=2):
                print(f"      ✅ {ip} - доступен!")
            else:
                print(f"      ❌ {ip} - недоступен")

    elif not check_tcp_port(host, port):
        print("   🔧 Устройство доступно, но веб-сервер не отвечает")
        print("   🔧 Проверьте что веб-сервер запущен на устройстве")
        print("   🔧 Попробуйте другие порты: 8080, 8000, 443")

        # Проверим популярные порты
        common_ports = [8080, 8000, 443, 8443]
        for test_port in common_ports:
            if check_tcp_port(host, test_port, timeout=2):
                print(f"      ✅ Порт {test_port} открыт - попробуйте http://{host}:{test_port}")

    elif not success:
        print("   🔧 TCP подключение работает, но HTTP запрос провалился")
        print("   🔧 Возможно нужны специальные заголовки или авторизация")
        print("   🔧 Попробуйте открыть устройство в браузере")

    else:
        print("   🎉 Все проверки прошли успешно!")
        print("   💡 Проблема может быть в самих тестах API")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        url = sys.argv[1]
    else:
        url = "http://192.168.4.1"

    diagnose_connection(url)