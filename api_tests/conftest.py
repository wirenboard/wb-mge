"""Pytest configuration and shared fixtures for WB-MGE API tests"""

import os
import signal
import socket
import subprocess
import time
from pathlib import Path

import pytest
import requests

from api_client import WBMGEAPI
from rtu_slave_helpers import ModbusRtuSlaveThread

PROJECT_ROOT = Path(__file__).parent.parent
QEMU_READY_TIMEOUT = 120
QEMU_READY_INTERVAL = 2

# Test files that reboot/restart the device (which resets the heap). They are
# deferred to the very end of the run by pytest_collection_modifyitems so the rest
# of the suite executes as one continuous, no-reboot working session. That long
# session is what 00_test_heap_session.py brackets to detect heap leaks.
# A test may also opt in via @pytest.mark.reboot (module- or function-level).
REBOOT_TEST_FILES = {
    "14_test_reboot.py",
    "22_test_ota.py",
    "30_test_wifi_perm_disable.py",
    "33_test_auth_settings.py",
    "40_test_web_port.py",
    "42_test_sniffer_cache_overlays_e2e.py",
}


def pytest_collection_modifyitems(config, items):
    """Order the run as:
        [heap baseline] + [continuous no-reboot body] + [heap-final leak check] + [reboot tests]

    Reboot tests are pushed to the end so the heap baseline/final pair brackets one
    long, uninterrupted working session (a reboot would reset the heap and void the
    leak comparison). Ordering is stable within each group, so the body keeps its
    original numeric file order. No-op for partial selections that contain neither
    heap marker (e.g. running a single test file).
    """
    def basename(item):
        return os.path.basename(str(getattr(item, "fspath", "")))

    def is_reboot(item):
        return item.get_closest_marker("reboot") is not None or basename(item) in REBOOT_TEST_FILES

    # --without-reboot (used by the coverage run): drop every reboot test up front.
    # A reboot zeroes the in-RAM gcov counters, so reboot tests must not run before
    # the end-of-session /gcov dump. is_reboot() is the single source of truth
    # (REBOOT_TEST_FILES + @pytest.mark.reboot) — nothing to mirror in the Makefile.
    if config.getoption("--without-reboot", default=False):
        deselected = [it for it in items if is_reboot(it)]
        if deselected:
            config.hook.pytest_deselected(items=deselected)
            items[:] = [it for it in items if not is_reboot(it)]

    baseline, body, final, reboot = [], [], [], []
    for it in items:
        if it.get_closest_marker("heap_baseline"):
            baseline.append(it)
        elif it.get_closest_marker("heap_final"):
            final.append(it)
        elif is_reboot(it):
            reboot.append(it)
        else:
            body.append(it)

    # Only reorder when the heap-session bracket is actually present; otherwise leave
    # the user's selection untouched (e.g. `pytest 14_test_reboot.py` alone).
    if baseline or final:
        items[:] = baseline + body + final + reboot


def pytest_addoption(parser):
    parser.addoption("--ip", default="localhost:8080", help="IP address of WB-MGE device")
    parser.addoption("--qemu", action="store_true", default=False,
                     help="Launch QEMU before tests, kill after")
    parser.addoption("--qemu-skip-build", action="store_true", default=False,
                     help="Skip 'make qemu-flash-image' (use existing build)")
    parser.addoption("--coverage-dump", default=None,
                     help="After the test session, GET /gcov and save the coverage stream to this path")
    parser.addoption("--without-reboot", action="store_true", default=False,
                     help="Deselect tests that reboot/restart the device "
                          "(by @pytest.mark.reboot or REBOOT_TEST_FILES)")


def quick_connection_test(base_url):
    """Quick connection check before running tests"""
    parsed = __import__('urllib.parse', fromlist=['urlparse']).urlparse(base_url)
    host = parsed.hostname or "localhost"
    port = parsed.port or 8080

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


# Ports that QEMU reserves on the host (must match hostfwd arguments in qemu_process fixture).
QEMU_HOST_PORTS = [8080, 8081, 50502, 50503, 50504, 5561, 5562]


def _check_no_stale_qemu():
    """Check for stale QEMU processes or occupied ports and abort if found.

    When --qemu is used, conftest always launches its own QEMU instance.
    Any pre-existing qemu-system-xtensa process or occupied hostfwd port is a
    hard error regardless of --qemu-skip-build (that flag only skips the
    firmware build step, not the stale-process check).

    To run tests against an already-running QEMU, do NOT use --qemu; use --ip
    pointing at the running instance instead.
    """
    # Check for running qemu-system-xtensa processes.
    result = subprocess.run(
        ["pgrep", "-f", "qemu-system-xtensa"],
        capture_output=True, text=True
    )
    stale_pids = result.stdout.strip().splitlines()

    # Check for occupied ports (QEMU hostfwd binds on localhost).
    occupied_ports = []
    for port in QEMU_HOST_PORTS:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1)
        if sock.connect_ex(("127.0.0.1", port)) == 0:
            occupied_ports.append(port)
        sock.close()

    if not (stale_pids or occupied_ports):
        return  # No conflict detected.

    lines = ["Stale QEMU detected — cannot start a new instance safely."]
    if stale_pids:
        lines.append(f"  Running QEMU PIDs: {', '.join(stale_pids)}")
    if occupied_ports:
        lines.append(f"  Occupied ports: {', '.join(str(p) for p in occupied_ports)}")
    lines.append("  Kill it with:")
    lines.append("    pkill -9 -f qemu-system-xtensa")
    pytest.exit("\n".join(lines), returncode=1)


def _get_qemu_bin_path():
    """Get QEMU binary path from make target."""
    result = subprocess.run(
        ["make", "-s", "qemu-bin-path"],
        cwd=PROJECT_ROOT, capture_output=True, text=True
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip()


def _wait_for_qemu_ready(base_url, proc):
    """Poll QEMU until HTTP is responding or process dies."""
    deadline = time.monotonic() + QEMU_READY_TIMEOUT
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            return False, f"QEMU process died during startup (exit code {proc.returncode})"
        try:
            requests.get(f"{base_url}/favicon.webp", timeout=2)
            elapsed = QEMU_READY_TIMEOUT - (deadline - time.monotonic())
            print(f"QEMU ready after {elapsed:.0f}s")
            return True, None
        except requests.exceptions.RequestException:
            time.sleep(QEMU_READY_INTERVAL)
    return False, f"QEMU did not become ready in {QEMU_READY_TIMEOUT}s"


def _dump_qemu_log(label):
    """Print QEMU log file contents with a header."""
    log_file = PROJECT_ROOT / "build/qemu_test.log"
    if not log_file.is_file():
        return
    print(f"\n{'=' * 60}")
    print(f"QEMU LOG ({label}):")
    print('=' * 60)
    print(log_file.read_text())


def _poll_tcp_connect(host: str, port: int, timeout: float = 5.0) -> bool:
    """Poll a TCP endpoint until it accepts connections or timeout expires.

    Returns True if connection succeeded within timeout, False otherwise.
    More reliable than time.sleep() in CI: adapts to actual server readiness.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(0.5)
        try:
            sock.connect((host, port))
            sock.close()
            return True
        except (ConnectionRefusedError, OSError, socket.timeout):
            sock.close()
            time.sleep(0.1)
    return False


def build_gateway_fixture(port_num: int, tcp_host_port: int, uart_tcp_port: int,
                           bridge_port: int, modbus: bool, fake_value: int = 0x1234):
    """Factory: returns a pytest fixture that configures a gateway on the given port.

    Args:
        port_num: RS-485 port number (1 or 2).
        tcp_host_port: Host-side TCP port (QEMU hostfwd destination).
        uart_tcp_port: QEMU UART chardev TCP port (e.g. 5561 for UART1).
        bridge_port: TCP port the gateway listens on inside the firmware.
        modbus: True for Modbus TCP gateway mode, False for transparent bridge.
        fake_value: Register value returned by the RTU slave for any register read.

    Returns:
        A pytest fixture function that yields a ModbusRtuSlaveThread (or None
        when modbus=False) and handles full setup/teardown.
    """
    @pytest.fixture
    def gateway_fixture(api):
        # Step 1: verify UART chardev is reachable
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.settimeout(3.0)
        try:
            probe.connect(("127.0.0.1", uart_tcp_port))
            probe.close()
        except (ConnectionRefusedError, OSError, socket.timeout):
            probe.close()
            pytest.skip(
                f"Cannot connect to UART chardev TCP port {uart_tcp_port}. "
                "QEMU may not expose this UART as TCP in this configuration."
            )

        # Step 2: save original settings
        resp = api.get_settings()
        assert resp.status_code == 200, f"GET /settings failed: {resp.status_code}"
        original_settings = resp.json()

        rs485_key = f"rs485_{port_num}"
        slave = None
        try:
            # Step 3: disable port first to release the UART driver
            resp = api.set_port_mode(port_num, "disabled")
            assert resp.status_code == 200, \
                f"Failed to disable port {port_num}: {resp.status_code}"
            time.sleep(0.3)

            # Step 3.5: free the target TCP port if the cache Modbus server holds it.
            # A bridge gateway and the cache server cannot share a port (the firmware
            # rejects such a config). In a long no-reboot run an earlier test may have
            # left the cache server on this very port (50504 is the shared forwarded
            # test port reused by both), so disable it before binding the bridge.
            # Without a reboot to reset it, set_port_mode(tcp_bridge) would otherwise
            # hit listen() EADDRINUSE / a rejected settings write.
            if (original_settings.get("cache_modbus_server_enabled")
                    and original_settings.get("cache_modbus_port") == bridge_port):
                resp = api.update_settings({"cache_modbus_server_enabled": False})
                assert resp.status_code == 200 and resp.json().get("success") is True, \
                    f"Failed to free port {bridge_port} from the cache server: {resp.text}"
                time.sleep(0.5)

            # Step 4: apply full RS-485 config with bridge sub-object
            port_settings = dict(original_settings.get(rs485_key, {}))
            port_settings["bridge"] = {
                "mode": "server",
                "port": bridge_port,
                "ip": "0.0.0.0",
                "modbus": modbus,
            }
            resp = api.update_settings({rs485_key: port_settings})
            assert resp.status_code == 200, \
                f"POST /settings failed: {resp.status_code}"
            result = resp.json()
            assert result.get("success") is True, \
                f"Settings update not successful: {result}"
            time.sleep(0.3)

            # Step 5: switch to tcp_bridge mode and wait for the port to open
            resp = api.set_port_mode(port_num, "tcp_bridge")
            assert resp.status_code == 200, \
                f"POST /ports/{port_num}/mode tcp_bridge failed: {resp.status_code}"
            # Poll instead of fixed sleep: wait for the gateway TCP port to bind
            ready = _poll_tcp_connect("127.0.0.1", tcp_host_port, timeout=5.0)
            assert ready, (
                f"Gateway did not start listening on host port {tcp_host_port} within 5 s"
            )

            # Step 6: start RTU slave (only for modbus=True)
            if modbus:
                slave = ModbusRtuSlaveThread(
                    host="127.0.0.1",
                    port=uart_tcp_port,
                    fake_value=fake_value,
                    connect_timeout=5.0,
                )
                slave.start()
                connected = slave.wait_connected(timeout=5.0)
                assert connected, (
                    f"RTU slave could not connect to UART chardev on port "
                    f"{uart_tcp_port} within 5 s"
                )

            # Step 7: yield slave (or None) to the test
            yield slave

        finally:
            # Step 8: restore settings
            api.set_port_mode(port_num, "disabled")
            time.sleep(0.3)

            restore_resp = api.update_settings(original_settings)
            # Use print instead of assert: assert in finally would mask the original
            # test failure with a teardown exception, hiding the real root cause.
            if restore_resp.status_code != 200:
                print(f"✗ Failed to restore settings: HTTP {restore_resp.status_code}")

            original_mode = original_settings.get(rs485_key, {}).get("port_mode", "disabled")
            api.set_port_mode(port_num, original_mode)
            time.sleep(0.3)

            # Step 9: stop the RTU slave thread
            if slave is not None:
                slave.stop()
                slave.join(timeout=3.0)
                # Use print instead of assert: assert in finally masks the original test failure.
                if slave.is_alive():
                    print(
                        f"✗ RTU slave thread on port {uart_tcp_port} did not stop within 3 s "
                        "(port leak!)"
                    )

    return gateway_fixture


@pytest.fixture(scope="session", autouse=True)
def qemu_process(request):
    """Launch and manage QEMU process. Active only with --qemu flag."""
    if not request.config.getoption("--qemu"):
        yield None
        return

    # --- Preflight: check for stale QEMU processes or occupied ports ---
    _check_no_stale_qemu()

    # --- Build ---
    if not request.config.getoption("qemu_skip_build"):
        print("Building QEMU flash image...")
        result = subprocess.run(
            ["make", "qemu-create-flash-image", "qemu-create-efuse-image"],
            cwd=PROJECT_ROOT,
        )
        if result.returncode != 0:
            pytest.exit("make qemu-create-flash-image failed", returncode=1)

    flash_bin = PROJECT_ROOT / "build/qemu_flash.bin"
    efuse_bin = PROJECT_ROOT / "build/qemu_efuse.bin"
    if not flash_bin.is_file() or not efuse_bin.is_file():
        pytest.exit("qemu_flash.bin or qemu_efuse.bin not found in build/", returncode=1)

    # --- Find QEMU binary ---
    qemu_bin = _get_qemu_bin_path()
    if not qemu_bin:
        pytest.exit("QEMU binary not found (make qemu-bin-path failed)", returncode=1)
    print(f"QEMU binary: {qemu_bin}")

    # --- Launch QEMU ---
    log_file = PROJECT_ROOT / "build/qemu_test.log"
    log_handle = open(log_file, "w")

    proc = subprocess.Popen(
        [
            qemu_bin,
            "-M", "esp32", "-m", "4M",
            "-drive", f"file={flash_bin},if=mtd,format=raw",
            "-drive", f"file={efuse_bin},if=none,format=raw,id=efuse",
            "-global", "driver=nvram.esp32.efuse,property=drive,value=efuse",
            "-global", "driver=timer.esp32.timg,property=wdt_disable,value=true",
            "-nic", ("user,model=open_eth,"
                     "hostfwd=tcp:127.0.0.1:8080-:80,"
                     "hostfwd=tcp:127.0.0.1:8081-:8081,"
                     "hostfwd=tcp:127.0.0.1:50502-:502,"
                     "hostfwd=tcp:127.0.0.1:50503-:503,"
                     "hostfwd=tcp:127.0.0.1:50504-:50504"),
            "-nographic",
            "-serial", "mon:stdio",
            "-serial", "tcp::5561,server,nowait",  # UART1 (RS485-1) exposed as TCP on port 5561
            "-serial", "tcp::5562,server,nowait",  # UART2 (RS485-2) exposed as TCP on port 5562
        ],
        stdout=log_handle, stderr=subprocess.STDOUT,
    )
    print(f"QEMU started (PID {proc.pid}), log: {log_file}")

    # --- Wait for ready ---
    ip = request.config.getoption("--ip")
    base_url = f"http://{ip}"

    ready, err = _wait_for_qemu_ready(base_url, proc)
    if not ready:
        proc.kill()
        proc.wait()
        log_handle.close()
        _dump_qemu_log("startup failed")
        pytest.exit(err, returncode=1)

    # --- Yield to tests ---
    yield proc

    # --- Optional: pull firmware coverage data before shutting QEMU down ---
    dump_path = request.config.getoption("--coverage-dump")
    if dump_path:
        try:
            resp = requests.get(f"{base_url}/gcov", timeout=30, stream=True)
            with open(dump_path, "wb") as fh:
                for chunk in resp.iter_content(8192):
                    fh.write(chunk)
            print(f"Coverage stream saved: {dump_path} ({os.path.getsize(dump_path)} bytes)")
        except Exception as exc:
            print(f"Coverage dump failed: {exc}")

    # --- Teardown ---
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
    log_handle.close()
    print(f"QEMU stopped (PID {proc.pid})")


def pytest_sessionfinish(session, exitstatus):
    """Dump QEMU log when tests fail."""
    if not session.config.getoption("--qemu", default=False):
        return
    if exitstatus != 0:
        _dump_qemu_log("tests failed")


@pytest.fixture(scope="session")
def is_qemu(request):
    """Returns True when running against a QEMU instance (--qemu flag)."""
    return request.config.getoption("--qemu", default=False)


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
