"""WB-MGE HTTP API client"""

import requests


class WBMGEAPI:
    def __init__(self, base_url="http://localhost:8080"):
        self.base_url = base_url
        self.session = requests.Session()

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
        self.session = requests.Session()
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

    def auth(self, login="admin", password="admin"):
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
        return self.session.post(f"{self.base_url}/settings", json=data, timeout=10)

    def start_wifi_scan(self):
        """Start WiFi scan"""
        return self.session.post(f"{self.base_url}/wifi_scan/start", timeout=10)

    def get_wifi_scan_results(self):
        """Get WiFi scan results"""
        return self.session.get(f"{self.base_url}/wifi_scan/results", timeout=10)

    def get_ap_clients(self):
        """Get list of AP clients"""
        return self.session.get(f"{self.base_url}/ap_clients")

    def get_static_file(self, path):
        """Get static file"""
        return self.session.get(f"{self.base_url}/{path}")

    def get_session(self):
        """Check session status"""
        return self.session.get(f"{self.base_url}/session")

    def logout(self):
        """Logout"""
        return self.session.post(f"{self.base_url}/logout")

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

    def set_port_mode(self, port_num, mode):
        """Set port mode via POST /ports/{port_num}/mode"""
        return self.session.post(
            f"{self.base_url}/ports/{port_num}/mode",
            json={"mode": mode},
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
        import time
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

            response = self.session.post(f"{self.base_url}/cmd", json=payload, timeout=10)
            print(f"Command {cmd} sent, status: {response.status_code}")

            return response
        except requests.exceptions.RequestException as e:
            print(f"Error sending command {cmd}: {e}")
            raise
