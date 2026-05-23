"""Pytest configuration and shared fixtures for WB-MGE API tests"""

import signal
import socket
import subprocess
import time
from pathlib import Path

import pytest
import requests

from api_client import WBMGEAPI

PROJECT_ROOT = Path(__file__).parent.parent
QEMU_READY_TIMEOUT = 120
QEMU_READY_INTERVAL = 2


def pytest_addoption(parser):
    parser.addoption("--ip", default="localhost:8080", help="IP address of WB-MGE device")
    parser.addoption("--qemu", action="store_true", default=False,
                     help="Launch QEMU before tests, kill after")
    parser.addoption("--qemu-skip-build", action="store_true", default=False,
                     help="Skip 'make qemu-flash-image' (use existing build)")


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
QEMU_HOST_PORTS = [8080, 50504]


def _check_no_stale_qemu(skip_build: bool = False):
    """Check for stale QEMU processes or occupied ports.

    When skip_build is False (default): any stale process or occupied port is a hard error —
    we are about to launch a new QEMU and it would fail to bind the ports.

    When skip_build is True: the user may have intentionally started QEMU manually before
    running tests. In this case we only warn, not abort — but still print the PID/port info
    so the user is aware of potential conflicts.

    NOTE: The --qemu flag means "launch QEMU yourself and manage its lifecycle".
    If you want to run tests against an already-running QEMU instance, do NOT
    use --qemu — just point --ip at the running instance (e.g. --ip localhost:8080).
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

    lines = []
    if skip_build:
        # User passed --qemu-skip-build: they may have started QEMU manually. Warn only.
        lines.append("WARNING: QEMU process or occupied ports detected (--qemu-skip-build is set).")
        lines.append("  If QEMU is already running intentionally, ignore this warning.")
        lines.append("  If you want to run tests without launching a new QEMU, use --ip instead of --qemu.")
    else:
        lines.append("Stale QEMU detected — cannot start a new instance safely.")

    if stale_pids:
        lines.append(f"  Running QEMU PIDs: {', '.join(stale_pids)}")
    if occupied_ports:
        lines.append(f"  Occupied ports: {', '.join(str(p) for p in occupied_ports)}")

    if skip_build:
        lines.append("  To kill stale QEMU: pkill -9 -f qemu-system-xtensa")
        print("\n".join(lines))  # Warning only, do not abort.
    else:
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


@pytest.fixture(scope="session", autouse=True)
def qemu_process(request):
    """Launch and manage QEMU process. Active only with --qemu flag."""
    if not request.config.getoption("--qemu"):
        yield None
        return

    # --- Preflight: check for stale QEMU processes or occupied ports ---
    _check_no_stale_qemu(skip_build=request.config.getoption("qemu_skip_build"))

    # --- Build ---
    if not request.config.getoption("qemu_skip_build"):
        print("Building QEMU flash image...")
        result = subprocess.run(
            ["make", "qemu-flash-image", "qemu-efuse-image"],
            cwd=PROJECT_ROOT,
        )
        if result.returncode != 0:
            pytest.exit("make qemu-flash-image failed", returncode=1)

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
            "-nic", "user,model=open_eth,hostfwd=tcp:127.0.0.1:8080-:80,hostfwd=tcp:127.0.0.1:50504-:50504",
            "-nographic",
            "-serial", "mon:stdio",
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
