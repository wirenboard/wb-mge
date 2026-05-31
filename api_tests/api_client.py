"""WB-MGE HTTP API client"""

import time

import requests


class _DelayedSession(requests.Session):
    """A requests.Session that sleeps 100ms before every request."""

    DELAY_S = 0.10

    def request(self, method, url, **kwargs):
        time.sleep(self.DELAY_S)
        return super().request(method, url, **kwargs)


class WBMGEAPI:
    DEFAULT_LOGIN = "admin"
    DEFAULT_PASSWORD = "admin"

    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        self.session = _DelayedSession()

        self.session.headers.update({
            'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
            'Accept': 'application/json, text/plain, */*',
            'Accept-Language': 'en-US,en;q=0.9',
            'Accept-Encoding': 'identity',
            'Connection': 'close',
            'Cache-Control': 'no-cache',
        })

        self.session.verify = False

        try:
            import urllib3
            urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
        except ImportError:
            pass

    def reconnect(self):
        """Close current session and create a new one, preserving auth cookies."""
        old_cookies = self.session.cookies.copy()
        self.session.close()
        self.session = _DelayedSession()
        self.session.headers.update({
            'User-Agent': 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
            'Accept': 'application/json, text/plain, */*',
            'Accept-Language': 'en-US,en;q=0.9',
            'Accept-Encoding': 'identity',
            'Connection': 'close',
            'Cache-Control': 'no-cache',
        })
        self.session.verify = False
        self.session.cookies.update(old_cookies)

    def auth(self, login=DEFAULT_LOGIN, password=DEFAULT_PASSWORD):
        """Authorization"""
        try:
            response = self.session.post(f"{self.base_url}/auth", json={
                "login": login,
                "pass": password
            }, timeout=10)
            return response
        except requests.exceptions.RequestException:
            raise

    def get_info(self):
        """Get device information"""
        return self.session.get(f"{self.base_url}/info", timeout=10)

    def get_settings(self):
        """Get settings"""
        return self.session.get(f"{self.base_url}/settings", timeout=10)

    def update_settings(self, data):
        """Update settings"""
        return self.session.post(f"{self.base_url}/settings", json=data, timeout=30)

    def start_wifi_scan(self):
        """Start WiFi scan"""
        return self.session.post(f"{self.base_url}/wifi_scan/start", timeout=10)

    def get_wifi_scan_results(self):
        """Get WiFi scan results"""
        return self.session.get(f"{self.base_url}/wifi_scan/results", timeout=10)

    def get_ap_clients(self):
        """Get list of AP clients"""
        return self.session.get(f"{self.base_url}/ap_clients", timeout=10)

    def get_static_file(self, path):
        """Get static file"""
        return self.session.get(f"{self.base_url}/{path}", timeout=10)

    def get_session(self):
        """Check session status"""
        return self.session.get(f"{self.base_url}/session", timeout=10)

    def logout(self):
        """Logout"""
        return self.session.post(f"{self.base_url}/logout", timeout=10)

    def get_uptime(self):
        """Get device uptime"""
        return self.session.get(f"{self.base_url}/uptime", timeout=10)

    def get_cache_status(self):
        """Get cache server status"""
        return self.session.get(f"{self.base_url}/cache/status", timeout=10)

    def get_cache_csv(self):
        """Get cached register map as CSV"""
        return self.session.get(f"{self.base_url}/cache/csv", timeout=10)

    def get_cache_json(self):
        """Get cached register map as JSON"""
        return self.session.get(f"{self.base_url}/cache/json", timeout=10)

    def get_hostname(self):
        """Get device hostname"""
        return self.session.get(f"{self.base_url}/hostname", timeout=10)

    def set_port_mode(self, port_num, mode, settle_retries=2, settle_s=0.5):
        """Set port mode via POST /ports/{port_num}/mode

        30s timeout: a port-mode change runs serial+bridge deinit and reinit,
        which under host CPU contention (QEMU on a busy host) can take several
        seconds — especially when the listen socket from the previous mode is
        still being released by lwIP and create_listen_socket() retries.

        Transient-ESP_FAIL retry: under sustained mode switching without a reboot
        (the firmware's create_listen_socket retries bind() a few times at 100 ms
        while the previous mode's TCP pcb is still being released by lwIP, then
        gives up and the handler returns HTTP 400 {"error":"ESP_FAIL"}), wait and
        retry to give lwIP more time to free the socket. Only this specific
        transient failure is retried; a 200 or any other 400 (e.g. validation
        errors on a bad mode string) is returned immediately and unchanged.
        """
        resp = None
        for attempt in range(settle_retries + 1):
            resp = self.session.post(
                f"{self.base_url}/ports/{port_num}/mode",
                json={"mode": mode},
                timeout=30
            )
            if resp.status_code != 400 or "ESP_FAIL" not in resp.text:
                return resp
            if attempt < settle_retries:
                time.sleep(settle_s)
        return resp

    def set_port_cache(self, port_num, enabled):
        """Enable/disable the per-port cache overlay via POST /ports/{port_num}/cache.

        The cache overlay is orthogonal to the transport mode: it can be toggled
        live without a port reinit, so a short timeout is sufficient.
        """
        return self.session.post(
            f"{self.base_url}/ports/{port_num}/cache",
            json={"enabled": enabled},
            timeout=10
        )

    def send_packet(self, port_num: int, hex_str: str):
        """Send a raw RTU hex frame to an RS-485 port via POST /ports/{N}/send"""
        return self.session.post(
            f"{self.base_url}/ports/{port_num}/send",
            json={"hex": hex_str},
            timeout=10
        )

    def get_wb_test(self):
        """Get WB test status"""
        return self.session.get(f"{self.base_url}/wb_test", timeout=10)

    def set_wb_test(self, clock_out: bool):
        """Set WB test clock_out"""
        return self.session.post(f"{self.base_url}/wb_test", json={"clock_out": clock_out}, timeout=10)

    def get_sniffer_status(self):
        """Get sniffer status for all ports"""
        return self.session.get(f"{self.base_url}/sniffer/status", timeout=10)

    def wait_for_ready(self, timeout=10, interval=1):
        """Poll the server until it responds, then reconnect and re-auth."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            time.sleep(interval)
            self.reconnect()
            try:
                self.auth()
                resp = self.get_info()
                if resp.status_code == 200:
                    return
            except requests.exceptions.RequestException:
                continue
        raise TimeoutError(f"Server did not come back within {timeout}s")

    def execute_command(self, cmd):
        """Execute command"""
        try:
            print(f"Sending command: {cmd}")
            payload = {"cmd": cmd}
            print(f"JSON payload: {payload}")

            response = self.session.post(f"{self.base_url}/cmd", json=payload, timeout=30)
            print(f"Command {cmd} sent, status: {response.status_code}")

            return response
        except requests.exceptions.RequestException as e:
            print(f"Error sending command {cmd}: {e}")
            raise
