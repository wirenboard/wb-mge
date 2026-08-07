"""Pytest configuration and shared fixtures for WB-MGE API tests"""

import os
import signal
import socket
import subprocess
import time
import warnings
from pathlib import Path

import pytest
import requests

from api_client import WBMGEAPI
from rtu_slave_helpers import ModbusRtuSlaveThread

PROJECT_ROOT = Path(__file__).parent.parent
QEMU_READY_TIMEOUT = 900
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


def _await_bridge_ready(host: str, port: int, hold: float = 0.5,
                        settle: float = 0.4, timeout: float = 15.0) -> bool:
    """Wait until a single-client bridge ADMITS a connection (boolean readiness).

    Why the old _poll_tcp_connect() was not enough: against a QEMU user-net
    (slirp) hostfwd port, slirp accept()s on the HOST before it forwards the SYN to
    the guest (see the connection-limit test's note), so connect()+close() returns
    True instantly regardless of firmware state — it never reaches the guest. This
    helper actually reaches the guest. For a transparent bridge (server mode,
    max_connections == 1) it confirms a connection is ADMITTED: held for `hold`
    seconds with no EOF (FIN) or RST — either of which means it was rejected by the
    cap or closed by a pending port deinit — then closes it and waits `settle`.

    HEURISTIC, not a guarantee: the freed-slot postcondition is NOT verified;
    `settle` is a sleep that NARROWS, but does not close, the handoff window between
    this probe's close and the caller's own subsequent connect(). Callers that
    connect-and-use should prefer _connect_ready_bridge(), which returns the
    admitted socket and removes the handoff by construction. This boolean form
    exists for fixtures, which yield and cannot hand a socket to the test.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        try:
            sock.connect((host, port))
        except OSError:
            sock.close()
            time.sleep(0.2)
            continue
        admitted = False
        try:
            sock.settimeout(hold)
            try:
                admitted = sock.recv(1) != b''   # b'' => FIN: rejected/closed
            except (socket.timeout, TimeoutError):
                admitted = True                  # held for `hold` with no EOF/RST
            except OSError:
                admitted = False                 # RST/ECONNRESET => rejected, retry
        finally:
            sock.close()
        if admitted:
            time.sleep(settle)                   # heuristic: let the slot free
            return True
        time.sleep(0.2)
    return False


def _connect_ready_bridge(host: str, port: int, hold: float = 0.5,
                          timeout: float = 15.0):
    """Return an ADMITTED, still-open socket to a single-client bridge.

    Preferred over _await_bridge_ready() wherever a test connects and then USES the
    connection: there is no probe-close-then-reconnect handoff, so the single-slot
    admit/free race cannot occur by construction (review point 4). Retries connect
    until one is admitted — held `hold` seconds with no EOF (FIN) or RST — and
    returns THAT open socket; the caller owns and closes it. Raises TimeoutError if
    no connection is admitted within `timeout`.

    On an idle bridge the admission check's recv() only ever times out (nothing is
    consumed from the stream), so the returned socket is clean for the caller's
    first send/recv.
    """
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3.0)
        try:
            sock.connect((host, port))
        except OSError:
            sock.close()
            time.sleep(0.2)
            continue
        try:
            sock.settimeout(hold)
            if sock.recv(1) == b'':              # FIN: rejected by cap / closed by deinit
                sock.close()
                time.sleep(0.2)
                continue
            # Unexpected data on an idle probe: the port is clearly up; accept it.
        except (socket.timeout, TimeoutError):
            pass                                 # held with no EOF/RST => admitted
        except OSError:                          # RST/ECONNRESET => rejected, retry
            sock.close()
            time.sleep(0.2)
            continue
        sock.settimeout(None)
        return sock                              # admitted, open, handed to the caller
    raise TimeoutError(
        f"bridge on {host}:{port} did not admit a connection within {timeout} s"
    )


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
            # Wait for the gateway to reliably admit AND free a connection, not just
            # bind for an instant: under load a single-slot bridge can still be
            # reconfiguring or holding the readiness probe's slot, which would reject
            # or close the test's connection and drop its serial->TCP bytes.
            ready = _await_bridge_ready("127.0.0.1", tcp_host_port, timeout=20.0)
            assert ready, (
                f"Gateway did not become stably ready on host port {tcp_host_port} within 20 s"
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
                     "hostfwd=tcp:127.0.0.1:50504-:50504,"
                     "hostfwd=udp:127.0.0.1:5570-:5570"),  # IO state bus (UDP)
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
def api(request, qemu_process):
    """Session-scoped API client: creates, checks connectivity, authenticates.

    Requests qemu_process without using the handle, on purpose. That fixture is what
    launches QEMU and waits for it to answer, and this one pytest.exit()s the entire
    session when the device does not respond — so it must not run first. Their order
    used to rest on nothing but pytest setting up session-scoped autouse fixtures
    before non-autouse ones at the same scope, which is not a documented guarantee.
    It stopped holding under pytest 9.1.1 once a module-scoped autouse fixture
    (_restore_rs485_settings) started depending on `api`: the suite died on its first
    item with "Preliminary connection check failed" before QEMU had been started.
    Harmless without --qemu: qemu_process yields None and the device is whatever --ip
    points at.
    """
    ip = request.config.getoption("--ip")
    base_url = f"http://{ip}"

    client = WBMGEAPI(base_url)

    if not quick_connection_test(base_url):
        pytest.exit("Preliminary connection check failed — check network connection", returncode=1)

    response = client.auth()
    assert response.status_code == 200
    assert response.json()["auth"] == True, "Initial authentication failed"

    return client


# Fields of an rs485_N object that a test module may clobber and that are safe to write
# back verbatim.
#
# Deliberately excludes port_mode and the bridge sub-object: validate_port_collisions()
# (main/settings_manager.c) marks a bridge listener as "touched" when the request carries
# bridge.port, rs485_N.port_mode or bridge.mode, and a touched collision is a hard
# rejection of the WHOLE request — so writing those back would turn a collision inherited
# from an earlier test file into a refused restore. Writing port_mode additionally
# re-initialises the running ports (main/settings_update.c), which is a side effect a
# restore must not have.
#
# Also deliberately excludes cache_en, even though it IS an rs485_N base field
# (rs485_base_mappings, main/settings_manager.c:91). port_manager_apply_cache_settings()
# (main/bridge/port_manager.c:1175) runs on every settings write regardless of what the
# request contained, but it is a no-op while the stored cache_en_N keys still agree with
# the port that actually holds the runtime overlay. Writing cache_en back is what can make
# them disagree, and a disagreement makes it call cache_move_locked() — which moves the
# overlay to the other port or tears the pool down, dropping every register value the cache
# had accumulated. A restore of serial line parameters has no business doing that.
_RS485_RESTORE_KEYS = ("tx_disabled", "baudrate", "stopbits", "parity",
                       "databits", "term", "fail_safe")

# Fields of _RS485_RESTORE_KEYS we refuse to inherit from NVS, mapped to the value written
# instead. The captured baseline is whatever the PREVIOUS run left behind (NVS survives the
# suite — see the caveat in _rs485_session_baseline), so without this the leak this fixture
# exists to stop becomes PERMANENT rather than merely forward-propagating: one run that
# ends with rs485_1.tx_disabled=True — a restore that failed, or a run interrupted inside
# any of the six files that set it (12, 16, 17, 20, 23, 34) — makes the next run capture
# True as its baseline and dutifully write True back after every single module.
# 13_test_ports.py::test_clock_out_keeps_rs485_2_de_low and
# 44_test_io_state_bus.py:69::test_rs485_direction_pins_idle_high then fail on every
# subsequent run, and re-running does not heal it. The removed per-file restore in
# 12_test_sniffer_ws.py had restore.setdefault("tx_disabled", False) for exactly this
# reason; this is that self-healing property, moved to where it covers the whole suite.
#
# ONLY for fields with an unambiguous known-good value. tx_disabled=False is both the
# firmware default (DEFAULT_485_TX_DISABLED "false", main/config.h:21) and the premise
# every DE/idle-HIGH test depends on. Do NOT add baudrate/parity/... here on the assumption
# that DEFAULT_485_* is what a module wants: those are genuine per-module choices, and
# pinning them would break files that legitimately run a non-default line configuration.
_RS485_SAFE_DEFAULTS = {"tx_disabled": False}

# Explicit HTTP timeout for this fixture pair's own requests, deliberately tighter than
# WBMGEAPI's 30 s default (api_client.py:79/:83).
#
# pytest-timeout charges setup + call + teardown of an item to ONE budget, and a
# module-scoped fixture is torn down inside the LAST item of its module — so the restore
# POSTs below land in some ordinary test's timeout budget, and several modules run
# per-test budgets of 30 s or less. Inheriting 30 s would let a single stalled request
# eat a whole item budget on its own and report as "that test timed out", with nothing
# pointing at conftest.
#
# A (connect, read) TUPLE, not a scalar. In requests a scalar timeout applies to EACH phase
# separately, and _DelayedSession sends Connection: close (api_client.py:31) so every call
# opens a fresh connection — a scalar 20 would therefore mean 20 s connect + 20 s read =
# 40 s worst case PER CALL, i.e. an ~80 s teardown, not the 40 s the markers quote. There
# are no retries to multiply that by: requests' HTTPAdapter defaults to max_retries=0.
#
# The split is asymmetric on purpose. Connect is a loopback TCP handshake to a QEMU
# hostfwd port — it is either immediate or never, so 5 s is already far past generous.
# The READ is the phase that can legitimately take tens of seconds, because /settings
# serialises ~50 NVS-backed fields under emulated-flash load (see the note in pytest.ini);
# 15 s covers that without letting one stalled response eat a whole item budget.
#
# Resulting ceilings, which the @pytest.mark.timeout comments across the suite quote:
#   per call : 0.1 s (_DelayedSession.DELAY_S) + 5 s connect + 15 s read = 20.1 s
#   teardown : 2 ports x 20.1 s + _RS485_RESTORE_SETTLE_S = 41.2 s  ("45 s allowance")
_RS485_HTTP_TIMEOUT = (5, 15)

# Settle window after the last restore POST — see the barrier note at the end of
# _restore_rs485_settings for why a bounded sleep and not another request.
_RS485_RESTORE_SETTLE_S = 1.0


@pytest.fixture(scope="session")
def _rs485_session_baseline(api):
    """Capture the whitelisted rs485_1/rs485_2 fields ONCE for the whole session.

    Deliberately session-scoped, not per-module. Two reasons:

    1. Cost. A per-module snapshot meant one GET /settings per module entry — 52 of them
       in a full run (50 test files, plus one extra entry for each file whose items are
       split across groups by pytest_collection_modifyitems: today 00_test_heap_session.py
       and 47_test_io_indication.py) — each charged to that module's FIRST test item,
       including modules whose first test runs a 20-30 s pytest-timeout budget. Capturing
       once removes the GET from every module's setup phase; only the teardown POSTs
       remain (see _RS485_HTTP_TIMEOUT), and those land in the LAST item instead.

    2. Semantics. Restoring to one fixed suite baseline means no module can inherit
       another module's rs485 state. A per-module snapshot did the opposite: it faithfully
       preserved whatever the previous module leaked, so contamination still propagated —
       it merely stopped growing within a file.

    Honest caveat: NVS persists across suite runs, so this baseline is whatever the
    PREVIOUS run left behind, not necessarily the firmware default. That is still a strict
    improvement (every module converges on one state instead of drifting through the run),
    but it does not by itself guarantee that state is the factory one. _RS485_SAFE_DEFAULTS
    plugs the one case where inheriting the previous run would be actively
    self-perpetuating rather than merely imprecise — see the comment on that dict.

    Returns a dict {"rs485_N": {field: value, ...}} ready to hand to POST /settings,
    or {} when nothing usable could be captured (in which case it warns).
    """
    saved = {}
    try:
        # The underlying session, not api.get_settings(), so the timeout is ours.
        resp = api.session.get(f"{api.base_url}/settings", timeout=_RS485_HTTP_TIMEOUT)
        status = resp.status_code
        before = resp.json()
    except Exception as exc:  # noqa: BLE001 - the snapshot is best-effort by design
        warnings.warn(
            f"_rs485_session_baseline: could not snapshot settings ({exc!r}); rs485 "
            f"settings will NOT be restored between test modules for this whole run.",
            stacklevel=1,
        )
        return saved

    # The status code is carried into every warning below on purpose. A 401 or a 500 still
    # answers with a JSON OBJECT, so it sails past the isinstance() check and lands in the
    # "carried no rs485_1/rs485_2" branch — which, without the code, reads like a firmware
    # that stopped reporting the ports rather than a request that was never authorised.
    # 33_test_auth_settings.py deliberately evicts the api fixture's own session from the
    # auth ring, so a 401 envelope is a realistic body here.
    if not isinstance(before, dict):
        # Warn rather than degrade silently: this is exactly the case where contamination
        # is most likely.
        warnings.warn(
            f"_rs485_session_baseline: GET /settings did not return a JSON object "
            f"(HTTP {status}, body={before!r}); rs485 settings will NOT be restored "
            f"between test modules for this whole run.",
            stacklevel=1,
        )
        return saved

    for port in ("rs485_1", "rs485_2"):
        port_settings = before.get(port)
        if not isinstance(port_settings, dict):
            continue  # key absent (older firmware / error body) — nothing to restore
        # _RS485_SAFE_DEFAULTS overrides the captured value for the fields we refuse to
        # inherit from NVS; everything else is written back verbatim. Still gated on the
        # key being present in the response, so a firmware that does not report a field
        # never gets one invented for it. .get() is safe for a False default — it
        # distinguishes a missing key from a falsy value.
        fields = {}
        for key in _RS485_RESTORE_KEYS:
            if key in port_settings:
                fields[key] = _RS485_SAFE_DEFAULTS.get(key, port_settings[key])
        if fields:
            saved[port] = fields

    if not saved:
        warnings.warn(
            f"_rs485_session_baseline: GET /settings carried no rs485_1/rs485_2 object "
            f"with any of {_RS485_RESTORE_KEYS} (HTTP {status}, body={before!r}); rs485 "
            f"settings will NOT be restored between test modules for this whole run.",
            stacklevel=1,
        )
    return saved


@pytest.fixture(scope="module", autouse=True)
def _restore_rs485_settings(_rs485_session_baseline, api, request):
    """Write the session rs485 baseline back after every test module. Teardown only.

    Why this exists (not just what it does):

    Nearly every test file has a module-scoped `_baseline` fixture that writes
    persistent NVS settings on the device, and almost none of them undo it. Because
    the whole suite runs against ONE long-lived QEMU instance without a reboot, every
    such write leaks forward into every later file, and the later file's failure looks
    like a firmware bug rather than contamination from a file it never mentions.

    That is not hypothetical. 12_test_sniffer_ws.py sets rs485_1.tx_disabled=True
    (required for the QEMU sniffer). With no teardown it leaked into
    13_test_ports.py::test_clock_out_keeps_rs485_2_de_low: bringing port 1 up calls
    serial_set_tx_disabled(true), which drives the DE GPIO LOW instead of letting it
    idle HIGH. That was misdiagnosed as a firmware regression for weeks. The currently
    exposed victim of the same class is
    44_test_io_state_bus.py:69::test_rs485_direction_pins_idle_high, which asserts
    exactly the same "DE idles HIGH" property and is equally defenceless against any
    earlier file leaving tx_disabled set.

    Fixing each `_baseline` one at a time only moves the next leak; this fixture kills
    the class. pytest sets a conftest-level fixture up before a test module's own
    same-scope autouse fixture and tears it down after, so this brackets every
    `_baseline` in the suite regardless of what that `_baseline` does.

    It has NO setup body on purpose — the snapshot lives in the session-scoped
    _rs485_session_baseline fixture, so nothing here is charged to a module's first test.

    Scope is deliberately narrow — only the two rs485_N objects, only the serial fields
    listed in _RS485_RESTORE_KEYS. Top-level settings are never touched:
    wifi_perm_disable is a one-way latch on real hardware, `pass` comes back in
    plaintext, and web_port always participates in collision validation.

    Note: any file whose items are split across groups by pytest_collection_modifyitems
    enters module scope more than once, so this fixture simply fires more than once for
    that file. Harmless. Today that is 00_test_heap_session.py (its heap_baseline and
    heap_final items are moved to the two ends of the run, around everything else) and
    47_test_io_indication.py (only test_factory_reset_long_press is @pytest.mark.reboot,
    so it joins the deferred reboot group while the file's other two items stay in the
    body).
    """
    yield

    # One POST PER PORT, never a single combined write. validate_rs485_settings()
    # (main/settings_manager.c:565-620) returns false on the FIRST invalid field and the
    # whole request is then rejected, so one bad legacy value in rs485_2 would silently
    # abandon the rs485_1 restore too. Separate requests keep the two independent, and
    # each failure is reported on its own.
    restored_any = False
    for port, fields in _rs485_session_baseline.items():
        # Teardown must WARN, never raise: it runs after the module's tests, so an
        # exception raised here would displace whatever real failure came before it.
        try:
            # The underlying session, not api.update_settings(), so the timeout is ours.
            resp = api.session.post(f"{api.base_url}/settings", json={port: fields},
                                    timeout=_RS485_HTTP_TIMEOUT)
            # Body parsing kept OUT of the outer except: a non-JSON body means the request
            # itself SUCCEEDED and answered with something unexpected, which is a different
            # diagnosis from "request failed" and must not be reported as one.
            try:
                body = resp.json()
            except ValueError:
                body = None
            # A REJECTED settings write answers HTTP 200 with {"success": false, ...}
            # (settings_manager.c:849-855 returns ESP_OK so the HTTP layer can send the
            # error JSON), so the status code alone proves nothing — the body must be
            # checked. isinstance() before .get(): a JSON list or scalar has no .get, and
            # the AttributeError would otherwise be swallowed by the outer except and
            # misreported as a failed request. `is True`, not `is not False`: the whole
            # point of this line is that the status code cannot be trusted, and a body
            # with no "success" key at all is not a settings response the firmware
            # produced on its happy path.
            ok = (resp.status_code == 200 and isinstance(body, dict)
                  and body.get("success") is True)
            detail = f"HTTP {resp.status_code}, body={body!r}"
        except Exception as exc:  # noqa: BLE001 - never let teardown mask a test failure
            ok = False
            detail = f"request failed: {exc!r}"
        if ok:
            restored_any = True
        else:
            warnings.warn(
                f"_restore_rs485_settings: failed to restore {port} after "
                f"{request.node.name} ({detail}); settings {fields} may leak into later "
                f"test files.",
                stacklevel=1,
            )

    if restored_any:
        # Barrier. When a restore genuinely CHANGES a line parameter — i.e. exactly in the
        # modules this fixture exists for — port_manager_check_settings_changed() returns
        # true and settings_update() (main/settings_update.c:442) spawns
        # settings_update_task, which releases and re-inits the port AFTER the POST
        # response has already been sent. Without a pause here the next module's first test
        # can start mid-re-init.
        #
        # Only the LAST POST is exposed. settings_update() self-synchronises on the NEXT
        # POST /settings (settings_update.c:368 spins while update_task_handle != NULL), so
        # the rs485_2 write already waits out the task the rs485_1 write spawned, and every
        # module with its own POST-based `_baseline` waits out ours. The modules that do not
        # are the ones that open a raw TCP/UART socket first — 13, 18, 43, 44-48 — and
        # 44:69::test_rs485_direction_pins_idle_high, one of the two tests this fixture was
        # written to protect, is among them.
        #
        # A bounded sleep, deliberately, and NOT a GET /info: /info would prove nothing.
        # It is answered by the httpd task, which the rs485 flags never release, and its
        # port fields come from port_manager_get_mode() (port_manager.c:833) and
        # port_manager_get_cache() (:980), both plain unlocked reads of pm_ctx — so it
        # returns immediately and happily reports a port that is mid-re-init. Nor would
        # polling help: neither field changes across a re-init, so there is nothing to
        # poll for. The only request that is a real barrier is another POST
        # /settings, and a third bounded call would push the teardown ceiling from 41.2 s
        # to 61.3 s, past the 45 s allowance every @pytest.mark.timeout comment in the
        # suite quotes. 1 s is the same settle window the suite already uses for a port
        # rebind (e.g. 20/31's cache fixtures) and comfortably covers the "few hundred
        # milliseconds" the release->acquire window is documented to take
        # (settings_update.c:231-244).
        time.sleep(_RS485_RESTORE_SETTLE_S)


@pytest.fixture(autouse=True)
def _uart_leak_guard(request):
    """Diagnostic-only guard: warn if a test left a QEMU UART socket leaked.

    QEMU exposes each RS-485 UART as a single-client TCP chardev (5561/5562).
    If a test wedges and leaks its UART socket, the one accept slot stays
    occupied and every later UART connect() times out. On teardown we probe
    both ports; if either is no longer connectable we warn (never fail — a
    fatal probe would make the whole suite fragile) naming the just-finished
    test as the likely leaker. Active only when running against QEMU.
    """
    yield
    if not request.config.getoption("--qemu", default=False):
        return
    unreachable = []
    for port in (5561, 5562):
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.settimeout(2.0)
        try:
            probe.connect(("127.0.0.1", port))
        except (ConnectionRefusedError, OSError, socket.timeout):
            unreachable.append(port)
        finally:
            try:
                probe.close()
            except OSError:
                pass
    if unreachable:
        warnings.warn(
            f"UART chardev port(s) {unreachable} not connectable after "
            f"{request.node.nodeid} — a prior test may have leaked a UART "
            f"socket (QEMU single-client).",
            stacklevel=1,
        )
