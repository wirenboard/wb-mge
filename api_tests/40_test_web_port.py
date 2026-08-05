"""web_port setting test — verifies that the HTTP server starts on the configured port after reboot.

Requires QEMU with hostfwd 8081->8081 (see conftest.py).
Skip on real hardware: no hostfwd guarantee exists there.
"""

import time

import pytest
import requests


# Port inside the firmware (NVS key web_port)
ALT_PORT_GUEST = 8081
# Corresponding host-side QEMU hostfwd port (127.0.0.1:8081 -> guest:8081)
ALT_PORT_HOST = 8081
# Default host-side port for the default web port mapping (127.0.0.1:8080 -> guest:80)
DEFAULT_PORT_HOST = 8080


def _wait_for_url(url, timeout=1800):
    """Poll url until an HTTP response is received or timeout expires. Returns True on success."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            requests.get(url, timeout=2)
            return True
        except requests.exceptions.RequestException:
            time.sleep(1)
    return False


def _is_reachable(url, timeout=5):
    """Returns True if url returns any HTTP response within timeout seconds."""
    try:
        requests.get(url, timeout=timeout)
        return True
    except requests.exceptions.RequestException:
        return False


@pytest.mark.timeout(2700)
def test_web_port_change(api, is_qemu):
    """web_port setting: after changing port and rebooting, server must be reachable on the new port only."""
    if not is_qemu:
        pytest.skip("Requires QEMU (hostfwd for alt port 8081)")

    from urllib.parse import urlparse
    host = urlparse(api.base_url).hostname or "localhost"

    default_url = f"http://{host}:{DEFAULT_PORT_HOST}"
    alt_url = f"http://{host}:{ALT_PORT_HOST}"

    # Precondition: server is reachable on the default port
    assert _is_reachable(default_url), \
        f"Server not reachable on default host port {DEFAULT_PORT_HOST} before test"

    try:
        # Change web_port and reboot
        resp = api.update_settings({"web_port": ALT_PORT_GUEST})
        assert resp.status_code == 200, f"POST /settings failed: {resp.status_code}"
        assert resp.json().get("success") is True, f"Settings update not successful: {resp.json()}"

        try:
            api.execute_command("reboot")
        except Exception:
            pass  # Connection drop during reboot is expected

        # Server must come up on the new port
        ready = _wait_for_url(f"{alt_url}/favicon.webp", timeout=1800)
        assert ready, (
            f"Server did not come up on alt host port {ALT_PORT_HOST} within 1800s "
            f"after setting web_port={ALT_PORT_GUEST} and rebooting"
        )

        # Old default port must be gone
        assert not _is_reachable(default_url, timeout=5), (
            f"Server still responds on default host port {DEFAULT_PORT_HOST} "
            f"after switching to web_port={ALT_PORT_GUEST}"
        )

    finally:
        # Restore web_port=80 via the alt port, then wait for default port to return
        try:
            restore = requests.Session()
            restore.post(f"{alt_url}/auth", json={"login": "admin", "pass": "admin"}, timeout=10)
            restore.post(f"{alt_url}/settings", json={"web_port": 80}, timeout=10)
            try:
                restore.post(f"{alt_url}/cmd", json={"cmd": "reboot"}, timeout=30)
            except Exception:
                pass  # Connection drop during reboot is expected
        except Exception as e:
            print(f"Warning: restore step failed: {e}")

        came_back = _wait_for_url(f"{default_url}/favicon.webp", timeout=1800)
        if came_back:
            api.reconnect()
            api.auth()
