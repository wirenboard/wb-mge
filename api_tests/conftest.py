"""Pytest configuration and shared fixtures for WB-MGE API tests"""

import socket
import pytest
import requests
from urllib.parse import urlparse

from api_client import WBMGEAPI


def pytest_addoption(parser):
    parser.addoption("--ip", default="192.168.5.1", help="IP address of WB-MGE device")


def quick_connection_test(base_url):
    """Quick connection check before running tests"""
    parsed = urlparse(base_url)
    host = parsed.hostname or "192.168.5.1"
    port = parsed.port or 80

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5)
        result = sock.connect_ex((host, port))
        sock.close()

        if result == 0:
            print(f"TCP connection to {host}:{port} successful")
            try:
                response = requests.get(base_url + "/favicon.webp", timeout=10,
                                        headers={
                                            'Accept-Encoding': 'identity',
                                            'Connection': 'close',
                                            'Cache-Control': 'no-cache',
                                        })
                print(f"HTTP test successful (Status: {response.status_code})")
                return True
            except Exception as e:
                print(f"TCP works, but HTTP failed: {e}")
                return False
        else:
            print(f"TCP connection to {host}:{port} failed")
            return False

    except Exception as e:
        print(f"Connection check error: {e}")
        return False


@pytest.fixture(scope="session")
def api(request):
    """Session-scoped API client: creates, checks connectivity, authenticates."""
    ip = request.config.getoption("--ip")
    base_url = f"http://{ip}"

    client = WBMGEAPI(base_url)

    if not quick_connection_test(base_url):
        pytest.exit("Preliminary connection check failed — check network connection", returncode=1)

    response = client.auth()
    assert response.status_code == 200
    assert response.json()["auth"] == True, "Initial authentication failed"

    return client
