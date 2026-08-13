"""Pytest configuration and shared fixtures for WB-MGE API tests"""

import ipaddress
import os
import signal
import socket
import subprocess
import time
import warnings
from pathlib import Path
from urllib.parse import urlsplit

import pytest
import requests

from api_client import WBMGEAPI
from rtu_slave_helpers import ModbusRtuSlaveThread
import qemu_ports
import tree_lock

# sniffer_helpers is a plain module, not a test file (pytest.ini's python_files matches only
# "[0-9]*_test_*.py"), so pytest does not rewrite its asserts by default — and its asserts are
# where the sniffer findings actually come from: _poll_sniffer_status() raises on behalf of
# each of its call sites in 13_test_ports.py. Registering it here restores rewritten output.
# This must run before anything imports the module; conftest is loaded ahead of every test
# module, and nothing above imports it.
pytest.register_assert_rewrite("sniffer_helpers")

PROJECT_ROOT = Path(__file__).parent.parent
QEMU_READY_TIMEOUT = 900
QEMU_READY_INTERVAL = 2

# QEMU stdout/monitor log path. NOT slot-suffixed, and that is safe only because the
# working-tree lock ENFORCES one --qemu run per working tree: build/qemu_flash.bin,
# build/qemu_efuse.bin and build/qemu_test_report.xml are per-tree make outputs and QEMU
# writes NVS back into qemu_flash.bin, so a second run in the same tree is refused before
# it can open this file — and, because `make qemu-test` takes that same lock around its
# whole recipe, before it can regenerate the flash image either. (It is opened with "w"
# below, i.e. truncated unconditionally —
# without that lock a sibling run would blank this log under a live QEMU that keeps writing
# at its old offset, which silently breaks 33_test_auth_settings.py's reboot-marker scan and
# 16_test_uart_teardown_crash.py's crash-marker scan. Neither would report a lock problem;
# one would just start passing for the wrong reason.)
# Keeping the fixed name also matches 33_test_auth_settings.py's _qemu_serial_log_path(),
# which stats this file to detect a reboot — a slot suffix here silently broke that
# reboot-detection on non-zero slots.
QEMU_LOG_PATH = PROJECT_ROOT / "build/qemu_test.log"

# Flash image this tree's QEMU runs. Defined here (not only inside qemu_process) because the
# preflight identifies "a QEMU belonging to THIS tree" by this exact path in its argv.
QEMU_FLASH_PATH = PROJECT_ROOT / "build/qemu_flash.bin"
QEMU_EFUSE_PATH = PROJECT_ROOT / "build/qemu_efuse.bin"

# Advisory whole-session lock that makes "one --qemu run per working tree" a rule the code
# enforces rather than a comment. Implemented in tree_lock.py, which is ALSO what qemu.mk
# runs, so `make qemu-test` holds the very same lock across its build steps and the pytest
# they feed — see that module for why the file sits in the repo root rather than under
# build/, and why the lock is Python's fcntl.flock rather than flock(1).
TREE_LOCK_PATH = tree_lock.LOCK_PATH

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
    parser.addoption("--ip", default=f"localhost:{qemu_ports.HTTP_HOST_PORT}",
                     help="IP address of WB-MGE device (default follows WB_MGE_PORT_SLOT)")
    parser.addoption("--qemu", action="store_true", default=False,
                     help="Launch QEMU before tests, kill after")
    parser.addoption("--qemu-skip-build", action="store_true", default=False,
                     help="Skip 'make qemu-flash-image' (use existing build)")
    parser.addoption("--coverage-dump", default=None,
                     help="After the test session, GET /gcov and save the coverage stream to this path")
    parser.addoption("--without-reboot", action="store_true", default=False,
                     help="Deselect tests that reboot/restart the device "
                          "(by @pytest.mark.reboot or REBOOT_TEST_FILES)")


# The active pytest Config, captured below. The UART-chardev helpers further down are
# plain module functions, not fixtures: they are called from inside tests and from the
# except handlers of helper threads, where there is no `request` to reach the config
# through — yet the "is this chardev ours?" rule has to read a CLI option (--ip). This
# hook is the one place pytest hands the config over outside a fixture.
_PYTEST_CONFIG = None


def pytest_configure(config):
    """Capture the session config for the module-level helpers (see _PYTEST_CONFIG)."""
    global _PYTEST_CONFIG
    _PYTEST_CONFIG = config

    # --qemu together with pytest-xdist is REFUSED, not merely discouraged. xdist workers
    # share one working tree by construction, so each would set up its own session-scoped
    # qemu_process against the same build/qemu_flash.bin and the same build/qemu_test.log —
    # the exact same-tree collision _acquire_tree_lock() exists to stop, except guaranteed
    # rather than accidental. (The lock would catch it, but the message below explains the
    # actual mistake instead of leaving the reader to work out why their workers refuse each
    # other.) Distinct per-worker port slots do NOT make this safe; see qemu_ports.py.
    # pytest-xdist is not in api_tests/requirements.txt, so this normally never fires.
    if config.getoption("--qemu", default=False):
        xdist_procs = getattr(config.option, "numprocesses", None)
        if xdist_procs or os.environ.get("PYTEST_XDIST_WORKER"):
            pytest.exit(
                "--qemu cannot be combined with pytest-xdist: every worker would launch "
                "its own QEMU against the SAME build/qemu_flash.bin and build/qemu_test.log "
                "in this one working tree. To run several suites at once, start one pytest "
                "process per WORKING TREE with its own WB_MGE_PORT_SLOT.",
                returncode=1,
            )

        # Taken HERE rather than in the qemu_process fixture, which runs after collection.
        # Two reasons: the barrier should rise before this session does anything at all,
        # and pytest_report_header (below) runs after this hook — so the header can state
        # what the lock IS doing instead of promising something not yet done. Released by
        # pytest_unconfigure, which pytest calls on every exit path including pytest.exit().
        _acquire_tree_lock()


def pytest_report_header(config):
    """Show the resolved port slot, where it came from, and the block it maps to.

    Printed for every run, including --ip ones. It is the only place a CI log answers
    "did the slot wiring actually take effect here?" — the whole scheme is invisible
    otherwise, and a silently-unset EXECUTOR_NUMBER would look identical to a working one.

    The tree line reports the OBSERVED lock state, not an intention: pytest_configure has
    already run by the time this hook is called, and _tree_lock_state() asks the kernel
    rather than trusting the marker — so "locked" here means the flock really is held, by
    this process or by the ancestor the line names.
    """
    header = f"qemu ports: {qemu_ports.port_summary()}"
    if config.getoption("--qemu", default=False):
        header += f"\nqemu tree : {PROJECT_ROOT} ({_tree_lock_state()})"
    return header


def pytest_unconfigure(config):
    """Drop the captured config so a finished session cannot answer for the next one.

    Matters when pytest runs more than once in a single process (pytester, an IDE
    runner): a stale Config would keep reporting the previous run's --ip/--qemu.

    Also where the working-tree lock is released — and the ONLY place, deliberately. It is
    tempting to drop it in qemu_process's teardown, "the QEMU is gone, nothing is written
    any more", but that is false: session-fixture teardown runs BEFORE pytest writes the
    --junitxml report and before pytest_sessionfinish, which is what reads and dumps
    build/qemu_test.log. The observed order is `session fixture teardown` (no XML yet) ->
    `sessionfinish` (XML written) -> `unconfigure`. Releasing early opens a window in which
    a second run truncates the log we are about to dump and then overwrites the report.
    This hook is also the one that runs on EVERY exit path, including the pytest.exit()s
    inside qemu_process that skip fixture teardown entirely. Idempotent either way.
    """
    global _PYTEST_CONFIG
    if _PYTEST_CONFIG is config:
        _PYTEST_CONFIG = None
    _release_tree_lock()


def quick_connection_test(base_url):
    """Quick connection check before running tests"""
    parsed = __import__('urllib.parse', fromlist=['urlparse']).urlparse(base_url)
    host = parsed.hostname or "localhost"
    port = parsed.port or qemu_ports.HTTP_HOST_PORT

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


# Ports that THIS run's QEMU reserves on the host — derived from WB_MGE_PORT_SLOT so
# that runs in separate trees occupy disjoint port blocks (see qemu_ports.py). Must match
# the hostfwd/-serial arguments the qemu_process fixture builds from qemu_ports. Split by
# transport: a UDP port cannot be probed with connect() (see _check_no_stale_qemu).
QEMU_HOST_TCP_PORTS = qemu_ports.MY_TCP_HOST_PORTS
QEMU_HOST_UDP_PORTS = qemu_ports.MY_UDP_HOST_PORTS


# This session's TreeLock, or None before it is taken / after it is released. Module-level
# rather than a fixture attribute so pytest_unconfigure can release it on EVERY exit path,
# including the pytest.exit()s inside qemu_process that never reach a fixture teardown.
_TREE_LOCK = None


def _acquire_tree_lock():
    """Take the exclusive per-WORKING-TREE lock, or abort naming the current holder.

    THE SLOT IS NOT THIS BARRIER. WB_MGE_PORT_SLOT makes two runs' HOST PORTS disjoint
    and says nothing about the FILES they share, which are per-tree and not per-slot:
    build/qemu_flash.bin (QEMU writes NVS back into it), build/qemu_efuse.bin,
    build/qemu_test_report.xml and build/qemu_test.log. Two runs in ONE tree with slots
    0 and 1 both sail through the port preflight and then clobber each other — most
    quietly through the log, which qemu_process opens with "w" (see QEMU_LOG_PATH).
    This lock is deliberately ORTHOGONAL to the slot, so runs in SEPARATE trees stay
    legal (that is the whole point of the slot) while two runs in ONE tree are refused.

    flock rather than the "is a foreign qemu-system-xtensa running" pgrep it replaces:
      * It covers the window BEFORE any QEMU exists — the build, the preflight, the
        several seconds until Popen. A process-list check sees nothing there, so two
        runs started together would both pass it.
      * It is released by the KERNEL when the holder dies, however it dies. A pid file
        or a marker file needs staleness heuristics; this needs none.
      * It is scoped to a FILE, i.e. to a tree, which is exactly the resource being
        protected — unlike the old global process check, which also refused legitimate
        runs in other trees and is what the slot work had to delete.
    The complementary case flock CANNOT see — an ORPHANED QEMU whose pytest died, so the
    lock is long released while the process still writes into this tree's flash image —
    is covered by _check_no_foreign_qemu_in_tree() below.

    Called from pytest_configure, i.e. BEFORE collection and before anything this session
    writes. `make qemu-test` normally holds the same lock already (qemu.mk wraps the whole
    recipe, build steps included, in tree_lock.py); this call then recognises the inherited
    lock instead of colliding with its own parent. For a bare `pytest --qemu` this IS the
    acquisition, and it still precedes the `make qemu-create-flash-image` that qemu_process
    runs. The mechanics, the file location and the flock caveats live in tree_lock.py.
    """
    global _TREE_LOCK
    if _TREE_LOCK is not None:
        return
    try:
        lock = tree_lock.TreeLock.acquire()
    except tree_lock.TreeLockBusy as exc:
        pytest.exit(
            "\n".join(tree_lock.busy_lines(
                exc,
                this_run=(f"pid={os.getpid()} slot={qemu_ports.SLOT} "
                          f"(from {qemu_ports.SLOT_SOURCE})"),
            )),
            returncode=1,
        )
    if not lock.enforced:
        warnings.warn(tree_lock.UNENFORCED_WARNING, stacklevel=1)
    _TREE_LOCK = lock


def _tree_lock_state():
    """One short phrase describing what the tree lock is doing, for the report header.

    The inherited branch VERIFIES rather than reports: `inherited` only means "an env marker
    said an ancestor holds it", and printing that as a fact is how the header ends up
    promising a barrier a dead wrapper already dropped. tree_lock.verify_intact() re-runs the
    marker checks and asks the kernel whether anyone still holds the flock.
    """
    if _TREE_LOCK is None:
        return "not locked"
    if _TREE_LOCK.inherited:
        problem = _TREE_LOCK.verify_intact()
        if problem:
            return f"NOT locked — {problem}"
        return f"locked by an ancestor process [{_TREE_LOCK.inherited_holder}]"
    if not _TREE_LOCK.enforced:
        return "NOT locked (no fcntl on this platform)"
    return f"locked for the session via {TREE_LOCK_PATH.name}"


def _verify_tree_lock_intact():
    """Abort if this session is no longer the only run allowed in this tree.

    Two failures, both of which the check used to pass over in silence:

    * The lock file was deleted or replaced while WE held it. An flock lives on an INODE, so
      anything that unlinks the path leaves the holder guarding an orphan while a second run
      locks the new file and proceeds. Nothing in the pipeline should touch the repo root —
      but the file is gitignored, and `git clean -xfd` deletes exactly that.
    * The ANCESTOR holding it for us is gone. On `make qemu-test` this session never takes
      the flock itself; it recognises the wrapper's. If the wrapper dies, the kernel frees
      the lock at once and nothing else here would ever notice.

    A loud abort, not a warning: continuing means writing build/qemu_flash.bin and
    build/qemu_test.log with no barrier while still reporting one in the header.
    """
    if _TREE_LOCK is None:
        return
    problem = _TREE_LOCK.verify_intact()
    if problem:
        pytest.exit(f"Working-tree lock lost: {problem}", returncode=1)


def _release_tree_lock():
    """Drop the tree lock. Idempotent; safe to call when it was never taken."""
    global _TREE_LOCK
    lock, _TREE_LOCK = _TREE_LOCK, None
    if lock is not None:
        lock.release()


def _qemu_processes_in_this_tree():
    """(matching ps lines, lookup error) for QEMUs whose -drive names THIS tree's image.

    The two failure modes are kept APART. An empty list with `error is None` means "the
    process list was read and holds no such QEMU"; a non-None error means "the process list
    could not be read", which is NOT evidence of anything and is reported as such by the
    caller. Collapsing both into [] — as this used to — reproduces the defect class of
    the `pgrep -af` it replaced: a check that passes silently on hosts where the tool is
    missing or refuses to run (procps is absent from many container images).

    `ps -Aww -o pid=,args=` rather than `pgrep -af`, which is not portable — on macOS
    pgrep's -a means something else entirely and the command prints bare pids, so the
    flash-path filter would match nothing and the check would silently pass on every
    developer machine. The `ww` is load-bearing too: without it ps may clip argv to the
    terminal width, and the flash path is not the first argument (it sits in `-drive
    file=...`), so a clipped line is another way to miss a live orphan.
    """
    try:
        res = subprocess.run(["ps", "-Aww", "-o", "pid=,args="],
                             capture_output=True, text=True, timeout=10, check=False)
    except (OSError, subprocess.SubprocessError) as exc:
        return [], f"could not run `ps`: {exc!r}"
    if res.returncode != 0:
        return [], (f"`ps` exited with {res.returncode}: "
                    f"{(res.stderr or '').strip() or '(no stderr)'}")
    # Match the WHOLE argument as QEMU spells it — `file=<flash image>,` — not a bare
    # substring of the path. A one-directional substring test also matches a FOREIGN tree
    # whose path merely ends with ours (/w/mge inside /home/ci/w/mge), and aborting a
    # legitimate run is as bad as missing an orphan. The trailing comma is what QEMU always
    # emits (`file=...,if=mtd,format=raw` — see qemu_process below and qemu.mk), so it
    # anchors the right-hand end; `file=` anchors the left.
    # Both path spellings: our own Popen passes PROJECT_ROOT unresolved, while a QEMU
    # started from a path with a symlinked component may show the resolved one.
    candidates = {f"file={QEMU_FLASH_PATH},", f"file={QEMU_FLASH_PATH.resolve()},"}
    return ([line.strip() for line in res.stdout.splitlines()
             if "qemu-system-xtensa" in line and any(c in line for c in candidates)],
            None)


def _check_no_foreign_qemu_in_tree():
    """Abort when a QEMU is already running against THIS tree's flash image.

    Covers the one case the tree lock cannot: an ORPHANED QEMU. If a previous pytest died
    (Ctrl-C at the wrong moment, an OOM kill) its flock went with it, but the QEMU it
    spawned can outlive it and keep writing NVS into build/qemu_flash.bin. A fresh run
    would then take the lock, regenerate that image under the orphan and, if the orphan
    sits in another slot, sail through the port preflight too.

    WHERE IT RUNS, exactly — because "covers" above would otherwise read as "prevents".
    This lives inside pytest, so on `make qemu-test` it fires AFTER the wrapper's
    prerequisites have already run `qemu-create-flash-image`, i.e. after esptool truncated
    the image under the orphan. What it buys there is a loud abort naming the process
    instead of a run that fails strangely later; only on a bare `pytest --qemu` (where the
    build happens below, inside this fixture) does it run before the damage. Moving it into
    tree_lock.main() would close that window for qemu-test — but the same wrapper also runs
    `make qemu-web`, whose whole job is to CLEAN UP such a QEMU, so it would have to become
    conditional; not worth it while the consequence is loud rather than silent.

    Deliberately NOT the old "any qemu-system-xtensa anywhere" check: that one refused
    legitimate sibling runs in other trees, which is exactly the capability the slot work
    exists to provide. Scoping the match to this tree's flash path keeps the guard and
    drops the false positives.

    A process list we could not READ warns and continues rather than aborting: this is the
    second line of defence behind the tree lock, so a machine without a usable `ps` must
    still be able to run the suite — but it must not be able to do so while claiming a
    guarantee it never checked.
    """
    mine, lookup_error = _qemu_processes_in_this_tree()
    if lookup_error:
        warnings.warn(
            f"Could not check for an orphaned QEMU in this tree ({lookup_error}). The "
            f"tree lock still stands, but nothing verified that a QEMU left behind by an "
            f"interrupted run is not still writing into {QEMU_FLASH_PATH}.",
            stacklevel=1,
        )
        return
    if not mine:
        return
    pytest.exit(
        "\n".join([
            "A QEMU process is already running against this tree's flash image.",
            f"  Flash image : {QEMU_FLASH_PATH}",
            "  Process(es) :",
            *(f"    {line}" for line in mine),
            "  It is most likely an orphan left by an interrupted run (its pytest is gone,",
            "  so the tree lock did not catch it). It still writes NVS into that image.",
            "  Kill it, e.g. (the pattern is anchored the same way the match above is, so",
            "  it cannot hit a QEMU in another tree whose path merely ends with ours):",
            f"    pkill -f 'file={QEMU_FLASH_PATH},'",
        ]),
        returncode=1,
    )


def _check_no_stale_qemu():
    """Abort only if THIS run's own host ports are occupied.

    When --qemu is used, conftest launches its own QEMU on the port block for this
    run's WB_MGE_PORT_SLOT. The preflight must be scoped to THAT block: with runs in
    several trees on one host, sibling QEMUs (other slots) are legitimate, so a global
    "any qemu-system-xtensa running" abort would make concurrent runs kill each other
    on preflight. We therefore check only OUR ports; a pre-existing process holding one
    of them is a real conflict (a stale run in the same slot, or an unrelated listener).

    TCP and UDP are probed DIFFERENTLY, because connect() does not answer the question
    for UDP: a datagram socket connect() only records a default peer, so connect_ex()
    returns 0 against a port nobody is bound to. The IO-bus UDP entry used to sit in the
    same connect_ex() loop as the TCP ports and therefore checked nothing at all. The
    real question — "can our QEMU bind this?" — is answered by trying to bind it, which
    is what QEMU's hostfwd does, without SO_REUSEADDR so an existing binder collides.

    Both probes are point-in-time: they cannot reserve anything, and something could
    still take a port between the probe and QEMU's own bind. That is a diagnosis-quality
    guarantee on purpose — the ownership guarantee comes from the tree lock plus the
    slot, and this check exists to turn "QEMU failed to set up host forwarding" into a
    message that names the port.

    To run tests against an already-running QEMU, do NOT use --qemu; use --ip pointing
    at the running instance instead.
    """
    occupied_ports = []
    for port in QEMU_HOST_TCP_PORTS:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(1)
        if sock.connect_ex((qemu_ports.HOST, port)) == 0:
            occupied_ports.append(port)
        sock.close()
    for port in QEMU_HOST_UDP_PORTS:
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.bind((qemu_ports.HOST, port))
        except OSError:
            occupied_ports.append(port)
        finally:
            sock.close()

    if not occupied_ports:
        return  # Our port block is free.

    lines = [
        f"Port conflict on slot {qemu_ports.SLOT} (from {qemu_ports.SLOT_SOURCE}) — "
        f"cannot start QEMU safely.",
        f"  Occupied ports (this run's block): "
        f"{', '.join(str(p) for p in sorted(occupied_ports))}",
        "  Another run in the same WB_MGE_PORT_SLOT, or an unrelated listener, holds "
        "them.",
        "  Use a different WB_MGE_PORT_SLOT, or kill the stale instance "
        f"(pkill -f 'file={QEMU_FLASH_PATH},' if it is a leaked QEMU from this tree).",
    ]
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
    log_file = QEMU_LOG_PATH
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


def _connect_ready_bridge(host: str, port: int, hold: float = 0.5,
                          timeout: float = 15.0):
    """Return an ADMITTED, still-open socket to a single-client transparent bridge.

    Why a plain connect() is not enough: against a QEMU user-net (slirp) hostfwd
    port, slirp accept()s on the HOST before it even forwards the SYN to the guest,
    so connect() succeeds instantly regardless of firmware state — the old
    _poll_tcp_connect readiness never reached the guest at all. This helper reaches
    the guest: for a transparent bridge (server mode, max_connections == 1) it
    confirms the connection was ADMITTED — the handshake COMPLETED and no EOF (FIN)
    or RST arrived within `hold` (either means the cap rejected it or a pending
    deinit closed it) — and RETURNS THAT open socket for the caller to use. Because
    the connection the test uses IS the one whose admission was confirmed, there is
    no probe-close-then-reconnect handoff and the single-slot race cannot occur by
    construction. Raises TimeoutError if no connection is admitted within `timeout` —
    a real failure the caller surfaces as a test FAILURE, never a skip.

    Admission detection is NEGATIVE (absence of FIN/RST within `hold`). The firmware
    listens with a backlog > 1, so lwIP completes the TCP handshake before the app
    accept()s; a just-opened connection can sit briefly in the accept queue —
    admitted at TCP level but not yet served — indistinguishable from a served one.
    That residue is acceptable: the caller's own first send/recv exercises the real
    path. The check uses MSG_PEEK, so it distinguishes FIN from data WITHOUT
    consuming anything — the returned socket is byte-for-byte clean for the caller
    (important where a test connects to the bridge before the serial side speaks).

    connect() and the hold-check are in SEPARATE try blocks on purpose. socket.timeout
    IS TimeoutError (an alias) and TimeoutError subclasses OSError, and connect() times
    out by raising TimeoutError — so a shared handler would misread a connect timeout
    (handshake never completed) as "held with no FIN => admitted" and return a dead
    socket. Only a timeout of the recv() AFTER a completed connect means "admitted".
    """
    # `timeout` bounds when we STOP STARTING attempts (a wall-clock deadline), not the
    # absolute return time: an attempt begun with just `hold` left runs the full,
    # deliberately-unclamped hold (see below), so the call can overshoot `timeout` by at
    # most `hold`. That is the only source of overshoot — connect and the backoff sleep
    # are both clamped to the remaining budget. We loop until the deadline at a
    # LIMITED FREQUENCY (backoff 0.2 -> 1.0 s between attempts) rather than a fixed
    # attempt count — a fixed count would silently shrink the real readiness window
    # (e.g. 8 attempts settle at ~6 s, so a port that opens at 7-14 s would be failed
    # though the caller asked for 15). Frequency-limiting still tames the churn: on a
    # rejected connection the FIN/RST returns instantly, so a naive spin would run
    # ~timeout/epsilon connect/close cycles on the single-slot bridge and ripple into
    # neighbouring ports; the backoff holds it to ~17 cycles over 15 s instead.
    if timeout <= hold:
        # `<=`, not `<`: `remaining` is computed AFTER `start`, so at timeout == hold the
        # first iteration already has remaining < hold and the floor below breaks before a
        # single attempt — the helper would raise "within 0.0 s", a confusing lie. Reject the
        # whole degenerate band up front. Unreachable today (smallest caller timeout is 2.0),
        # but fail loudly for the next caller instead of silently doing nothing.
        raise ValueError(
            f"timeout ({timeout:.3f} s) must be > hold ({hold:.3f} s): a shorter budget "
            f"cannot run even one trustworthy admission attempt"
        )
    start = time.monotonic()
    deadline = start + timeout
    backoff = 0.2
    while True:
        remaining = deadline - time.monotonic()
        # Floor: an attempt needs the FULL `hold` to reliably observe a FIN/RST. With
        # less than `hold` left, a degraded sub-`hold` peek could miss the reject and
        # falsely report "admitted" — the exact failure mode this helper exists to kill.
        # So stop rather than run a probe we can't trust.
        if remaining < hold:
            break
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        rejected = False
        try:
            sock.settimeout(min(3.0, remaining))
            sock.connect((host, port))
        except OSError:                          # refused / RST / connect timeout => retry
            rejected = True
        else:
            try:
                # Full `hold`, deliberately NOT clamped to the remaining budget: a
                # shortened peek could miss a late FIN/RST and falsely report "admitted".
                # connect() above may have eaten into the budget since the `remaining >=
                # hold` floor was checked, so this is what can push the call up to `hold`
                # past `timeout` (documented at the top). A false reject on overshoot is
                # acceptable; a false admit is the bug we refuse to reintroduce.
                sock.settimeout(hold)
                # MSG_PEEK: b'' => FIN (rejected/closed); data => up and already pushing
                # (left in the buffer for the caller); neither within `hold` => admitted.
                if sock.recv(1, socket.MSG_PEEK) == b'':
                    rejected = True
            except (socket.timeout, TimeoutError):
                pass                             # held `hold` with no FIN/RST => admitted
            except OSError:                      # RST during hold => retry
                rejected = True
        if not rejected:
            sock.settimeout(None)
            return sock                          # admitted, open, byte-clean for the caller
        sock.close()
        # Back off before retrying (never after a success — that path returns above),
        # clamped so we never sleep past the deadline. This can still sleep once after what
        # turns out to be the final attempt: whenever the sleep leaves < hold on the clock
        # (whether it started below hold, or started at remaining >= hold and this nap drops
        # it under — e.g. remaining 1.4 -> nap 1.0 -> 0.4), the next iteration's `remaining <
        # hold` floor fires and we stop. That wasted tail is bounded by `backoff` (<= 1 s),
        # not `hold`, but it never crosses the deadline (nap is clamped to the remaining budget).
        nap = min(backoff, deadline - time.monotonic())
        if nap <= 0:
            break
        time.sleep(nap)
        backoff = min(backoff * 2, 1.0)
    raise TimeoutError(
        f"bridge on {host}:{port} did not admit a connection within "
        f"{time.monotonic() - start:.1f} s (budget {timeout:.1f} s)"
    )


# Host QEMU binds the UART chardevs on (see the -serial arguments in qemu_process).
# Aliased from qemu_ports rather than spelled out again: that module is what builds the
# -serial arguments, and a second literal here is a second source of truth that can drift
# from the one the QEMU command line actually uses.
# Only a DEFAULT: call sites that carry their own host variable pass it explicitly, and the
# two chardevs live on different ports (qemu_ports.UART1_TCP_PORT / UART2_TCP_PORT), so
# neither the host nor the port may be baked into the helpers below.
UART_CHARDEV_HOST = qemu_ports.HOST

# The one exact phrase _uart_leak_guard prints, kept as a constant because the hint
# below tells the reader to grep for it: with two independently written strings, the
# hint told people to search for 'leaked the QEMU UART chardev' while the guard printed
# 'leaked a QEMU UART chardev', and the exact-phrase search they were told to run found
# nothing. Interpolating one constant into both makes that class of drift impossible.
UART_CHARDEV_LEAK_MARKER = "QEMU UART chardev stopped accepting connections"


def _endpoint_host(ip_option) -> str:
    """Host part of an --ip value: 'localhost:21000', '10.0.0.5', '[::1]:21000', a URL.

    Returns '' for anything unparseable — callers treat that as "not local", i.e. the
    conservative answer (skip rather than fail).
    """
    raw = (ip_option or "").strip()
    if not raw:
        return ""
    # urlsplit only parses an authority when one is marked as such; '//' marks it. It
    # then does the two fiddly parts for us: dropping the :port and unwrapping the IPv6
    # bracket form ('[::1]:21000' -> '::1', which a naive rsplit(':') would mangle).
    if "//" not in raw:
        raw = "//" + raw
    try:
        return (urlsplit(raw).hostname or "").strip()
    except ValueError:      # malformed authority, e.g. an unclosed '['
        return ""


def _is_local_host(host: str) -> bool:
    """True when `host` names THIS machine, so a QEMU here is the device under test."""
    host = (host or "").strip().rstrip(".").lower()
    if not host:
        return False
    # RFC 6761: 'localhost' and anything under '.localhost' always resolve to loopback.
    if host == "localhost" or host.endswith(".localhost"):
        return True
    try:
        addr = ipaddress.ip_address(host)
    except ValueError:      # a real hostname (or garbage) — not resolved on purpose:
        return False        # a DNS lookup here would be a surprise cost in a helper.
    # is_unspecified: connecting to 0.0.0.0 / :: reaches this machine, same as loopback.
    return addr.is_loopback or addr.is_unspecified


def _uart_chardev_is_ours(is_qemu: bool = False, config=None) -> bool:
    """True when this slot's two UART chardevs belong to THIS run's device under test.

    The rule is "the device is a QEMU on this machine", which is --qemu (conftest starts
    that QEMU itself) OR an --ip naming this machine (_is_local_host — the documented way
    to drive an already-running QEMU; see uart_chardev_unreachable for why the flag alone
    is the wrong signal).

    `config` is passed explicitly by fixtures, which have one; plain helpers fall back to
    the config captured in pytest_configure. Both `is_qemu` and the option are consulted
    so a call site that already holds the fixture value keeps working even if the capture
    is missing (a conftest imported outside a pytest session).
    """
    if is_qemu:
        return True
    config = _PYTEST_CONFIG if config is None else config
    if config is None:
        return False
    try:
        if config.getoption("--qemu", default=False):
            return True
        ip_option = config.getoption("--ip", default="")
    except (ValueError, AttributeError):
        return False
    return _is_local_host(_endpoint_host(ip_option))

# Appended to every under-QEMU "chardev unreachable" failure. The test that trips it is
# almost never the culprit — see _uart_leak_guard for what it can and cannot tell you.
_UART_CHARDEV_LEAK_HINT = (
    "This run's QEMU creates this chardev (-serial tcp::PORT,server,nowait) — conftest "
    "itself under --qemu, or whoever started the QEMU that a loopback --ip points at — "
    "and it accepts exactly ONE client, so 'not connectable' is not an environment "
    "property — it means an earlier test leaked its UART socket (typically a daemon "
    "helper thread whose stop() was skipped by an exception) and still holds the single "
    "accept slot. This test is a downstream victim, not the defect: search this run's "
    f"output for the MOST RECENT '{UART_CHARDEV_LEAK_MARKER}' warning that names the "
    "SAME port as this failure — the guard warns per port and prints the port numbers "
    "— because _uart_leak_guard emits it with the last few tests that ran, and the leak "
    "is in one of them. Deliberately not the earliest such warning: a healthy "
    f"run already contains one benign UART1 (port {qemu_ports.UART1_TCP_PORT}) warning "
    "from 20_test_cache_tcp_framing.py, "
    "whose module-scoped cache_tcp_server legitimately holds that chardev across all "
    "six of its tests, and that module starts 74 items into a 229-item run — so for "
    "anything that fails later, the earliest warning is reliably the wrong one. "
    "It cannot narrow it further; see that fixture's docstring for why."
)


def uart_chardev_unreachable(uart_tcp_port: int, is_qemu: bool,
                             host: str = UART_CHARDEV_HOST, detail=None):
    """Report an unreachable QEMU UART chardev. Never returns — always raises.

    The outcome is split by WHO OWNS THE CHARDEV, not by the --qemu flag:

    * When the device under test is a QEMU on THIS machine, the chardev exists and takes
      exactly one client, so unreachable == leaked == a defect of THIS suite, and it
      FAILS. Degrading it to a skip is how one leaked socket once turned 1 skipped test
      into 47 across nine files while the run still looked greener than the baseline —
      skips are not failures.
    * Against a REMOTE --ip (a real device on the LAN) there is no QEMU chardev on this
      host at all, so skipping is legitimate and stays.

    "A QEMU on this machine" is `--qemu OR a loopback --ip` (_uart_chardev_is_ours), and
    the second half is not a corner case: the documented way to test an ALREADY-RUNNING
    QEMU is exactly `pytest --ip localhost:<web port>` with NO --qemu, because --qemu means
    "launch and manage QEMU yourself" and aborts on the ports it finds occupied
    (README_QEMU.md, "Running tests against an already-running QEMU";
    api_tests/README_API_Tests.md). The --ip DEFAULT is loopback too — it is
    localhost:qemu_ports.HTTP_HOST_PORT, i.e. it follows the port slot (see
    pytest_addoption) — so a bare
    `pytest api_tests/` lands in the same mode. Keying the decision on --qemu alone left
    every such local run silently skipping — the exact cascade this helper exists to
    stop — and covered only the `make qemu-test` path.

    Both halves of that argument survived the move to slot-derived ports because BOTH sides
    move together: `make qemu-web` builds its hostfwd rules from qemu_ports too (see
    qemu.mk), so the already-running QEMU listens on the same block the default --ip points
    at, as long as both are run with the same WB_MGE_PORT_SLOT. Only the port NUMBERS
    changed; the "loopback endpoint means the chardev is ours" rule did not.

    The one case this deliberately gets wrong is a REAL device reached through a local
    port-forward (`ssh -L <local>:device:80` plus a loopback --ip): the endpoint is
    loopback but the chardev genuinely is not ours, so a test that needs UART TCP fails
    instead of skipping. Nothing in this repo documents that setup, and the failure is
    loud and self-explaining (the text below names the chardev port), which is the trade
    this helper exists to make.

    `detail` is the exception (or any object) that proves the endpoint is dead; it is
    appended verbatim so the reader sees the real errno rather than a paraphrase.
    """
    # With --tb=short the traceback would otherwise point at the pytest.fail below for
    # all 31 call sites; hiding this frame makes it point at the test that needed the
    # chardev, which is the only part of it the reader cannot already guess.
    __tracebackhide__ = True
    where = f"{host}:{uart_tcp_port}"
    suffix = f" ({detail})" if detail is not None else ""
    if _uart_chardev_is_ours(is_qemu):
        pytest.fail(f"UART chardev {where} is not connectable{suffix}. "
                    f"{_UART_CHARDEV_LEAK_HINT}")
    pytest.skip(
        f"UART chardev TCP port {uart_tcp_port} not reachable on {host}{suffix}; "
        "the device under test is remote (--ip), so there is no QEMU UART chardev here"
    )


def require_uart_chardev(uart_tcp_port: int, is_qemu: bool,
                         host: str = UART_CHARDEV_HOST, timeout: float = 3.0):
    """Return a CONNECTED probe socket to a QEMU UART chardev, or fail/skip trying.

    The single entry point for "this test needs the UART chardev". Failure semantics live
    in uart_chardev_unreachable() — fail when the chardev is this run's own (a QEMU on
    this machine, whether started by --qemu or merely pointed at by a loopback --ip),
    skip only against a remote device — so no call site has to re-decide them, and none
    of them may re-add a pytest.skip of its own.

    OWNERSHIP: the returned socket is the caller's, and it is deliberately NOT closed
    here, so that a caller which wants to KEEP the connection can (18_test_uart_chardev
    must hold the chardev open across a port-mode switch so QEMU buffers the TX bytes it
    is about to check; handing it a socket that was already closed and asking it to
    reconnect would race that switch).

    Most callers do not want the connection, only the reachability answer, and the
    `require_uart_chardev(...).close()` one-liner followed by a reconnect through some
    other helper (_UartEchoThread, PacketInjector, ModbusRtuSlaveThread) is the normal,
    supported shape — roughly every call site outside 18 is written that way. It is safe
    because nothing competes for the slot inside a single test: close() releases it and
    the helper's connect() takes it back. Do not read this note as a warning against that
    pattern; the point is only that the choice belongs to the caller, not to this helper.

    Callers that cannot use this (they open the chardev through another helper, e.g.
    packet_injector.open_uart_socket) call uart_chardev_unreachable() from their own
    except handler instead — same policy, no second connect.
    """
    # See uart_chardev_unreachable: keep this frame out of the --tb=short traceback so
    # the failure names the call site rather than this helper.
    __tracebackhide__ = True
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    probe.settimeout(timeout)
    try:
        probe.connect((host, uart_tcp_port))
    except (ConnectionRefusedError, OSError, socket.timeout) as exc:
        probe.close()
        # Raises: pytest's Skipped/Failed derive from BaseException, so this is not
        # swallowed by any `except Exception` around the call site.
        uart_chardev_unreachable(uart_tcp_port, is_qemu, host=host, detail=exc)
    return probe


def build_gateway_fixture(port_num: int, uart_tcp_port: int,
                          bridge_port: int, modbus: bool, fake_value: int = 0x1234):
    """Factory: returns a pytest fixture that configures a gateway on the given port.

    Args:
        port_num: RS-485 port number (1 or 2).
        uart_tcp_port: QEMU UART chardev TCP port (qemu_ports.UART1_TCP_PORT for UART1).
        bridge_port: GUEST-side TCP port the gateway listens on inside the firmware,
            i.e. the '-:<N>' side of a hostfwd rule (qemu_ports.*_GUEST_PORT). The HOST
            port that reaches it is the caller's business — see the note below.
        modbus: True for Modbus TCP gateway mode, False for transparent bridge.
        fake_value: Register value returned by the RTU slave for any register read.

    Returns:
        A pytest fixture function that yields a ModbusRtuSlaveThread (or None
        when modbus=False) and handles full setup/teardown.

    There is deliberately NO host-port parameter. One used to be declared and documented
    (`tcp_host_port`) while the body never read it: the readiness probe that once used it
    was removed as a slirp no-op (see Step 5), and every test connects to the host port
    through its own module constant. A parameter that eleven call sites pass and nothing
    consumes is not documentation, it is a claim the reader has to disprove — so it is
    gone rather than given a token use.
    """
    @pytest.fixture
    def gateway_fixture(api, is_qemu):
        # Step 1: verify UART chardev is reachable (fails when the QEMU is ours, skips
        # against a remote device — see require_uart_chardev). The probe is only a
        # reachability question here, so it is closed immediately, as before.
        require_uart_chardev(uart_tcp_port, is_qemu).close()

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
            # left the cache server on this very port (guest 50504 is the shared forwarded
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
            # No bridge-readiness PROBE here, on purpose. The old _poll_tcp_connect was
            # a slirp no-op (accepts host-side before the guest sees the SYN), and
            # a held readiness probe churns the single client slot and, under
            # CI jitter, ripples into the shared single-client UART chardev — turning
            # unrelated tests into spurious "chardev unreachable" SKIPs. Readiness is
            # now established where it belongs: at the test's own connection, via
            # _connect_ready_bridge(), which retries until the guest admits it and
            # RAISES (a real failure, never a skip) if it cannot.

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
            # Step 8: restore settings, then Step 9: stop the RTU slave thread.
            # Each restore call is a 30 s-read-timeout /settings request that can ReadTimeout
            # under QEMU load. Two invariants: (a) one failing call must not skip the others
            # (best-effort, each in its own try) and must not MASK the test's real error
            # (print, never raise); (b) slave.stop() must ALWAYS run — it is a daemon
            # ModbusRtuSlaveThread holding a live TCP connection to the single-client QEMU
            # chardev (UART1/UART2), so a leaked one wedges that chardev and cascades skips
            # across every module using this factory. So slave.stop() lives in a finally
            # wrapped around the restore block.
            try:
                try:
                    api.set_port_mode(port_num, "disabled")
                    time.sleep(0.3)
                except Exception as exc:
                    print(f"✗ teardown set_port_mode(disabled) failed: {exc!r}")

                try:
                    restore_resp = api.update_settings(original_settings)
                    if restore_resp.status_code != 200:
                        print(f"✗ Failed to restore settings: HTTP {restore_resp.status_code}")
                except Exception as exc:
                    print(f"✗ teardown update_settings failed: {exc!r}")

                try:
                    original_mode = original_settings.get(rs485_key, {}).get("port_mode", "disabled")
                    api.set_port_mode(port_num, original_mode)
                    time.sleep(0.3)
                except Exception as exc:
                    print(f"✗ teardown set_port_mode(restore) failed: {exc!r}")
            finally:
                if slave is not None:
                    slave.stop()
                    slave.join(timeout=3.0)
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

    # --- Preflight, in order of what each rules out ---
    # 1. This tree: a second live run here is refused whatever its slot. Already taken in
    #    pytest_configure — before collection, and before the build below, which rewrites
    #    the very image a sibling run would be using. Repeated here as a no-op assertion of
    #    the ordering: everything after this line assumes the tree is ours.
    _acquire_tree_lock()
    # 2. This tree, orphaned QEMU: a process still using our flash image with no live pytest.
    #    Note the ordering this does NOT achieve on `make qemu-test`, where the flash image
    #    was already rebuilt in the wrapper's prerequisites — see the docstring.
    _check_no_foreign_qemu_in_tree()
    # 3. This slot: our host port block must be free.
    _check_no_stale_qemu()

    # --- Build ---
    if not request.config.getoption("qemu_skip_build"):
        print("Building QEMU flash image...")
        result = subprocess.run(
            ["make", "qemu-create-flash-image", "qemu-create-efuse-image"],
            cwd=PROJECT_ROOT, check=False,
            # Hand the child the "this tree is already locked by pid N" marker, so any make
            # target it touches that takes the lock itself sees an ancestor holding it
            # rather than colliding with us. Nothing under this call does today; passing it
            # is what keeps that true if a build target is ever wrapped in run_locked.
            env=_TREE_LOCK.child_env() if _TREE_LOCK is not None else None,
        )
        if result.returncode != 0:
            pytest.exit("make qemu-create-flash-image failed", returncode=1)

    # Outside the branch above, deliberately. The build is the step most likely to cost a
    # lock file its inode (a stale hardware CMakeCache makes it run `idf.py fullclean`) —
    # but on `make qemu-test`, the CI and default developer path, that build ran in the
    # wrapper's prerequisites and pytest is invoked with --qemu-skip-build, so a check
    # nested in the branch never executed there at all. It is also the point where an
    # INHERITED lock is worth re-checking: the wrapper could have died between
    # pytest_configure and here, and everything below this line writes into the tree.
    # tree_lock.main() runs the same check when the wrapped command exits.
    _verify_tree_lock_intact()

    flash_bin = QEMU_FLASH_PATH
    efuse_bin = QEMU_EFUSE_PATH
    if not flash_bin.is_file() or not efuse_bin.is_file():
        pytest.exit("qemu_flash.bin or qemu_efuse.bin not found in build/", returncode=1)

    # --- Find QEMU binary ---
    qemu_bin = _get_qemu_bin_path()
    if not qemu_bin:
        pytest.exit("QEMU binary not found (make qemu-bin-path failed)", returncode=1)
    print(f"QEMU binary: {qemu_bin}")

    # --- Launch QEMU ---
    log_file = QEMU_LOG_PATH
    log_handle = open(log_file, "w")

    proc = subprocess.Popen(
        [
            qemu_bin,
            "-M", "esp32", "-m", "4M",
            "-drive", f"file={flash_bin},if=mtd,format=raw",
            "-drive", f"file={efuse_bin},if=none,format=raw,id=efuse",
            "-global", "driver=nvram.esp32.efuse,property=drive,value=efuse",
            "-global", "driver=timer.esp32.timg,property=wdt_disable,value=true",
            # hostfwd rules + UART chardev ports are derived from WB_MGE_PORT_SLOT
            # (qemu_ports) so parallel runs use disjoint host ports; guest ports are fixed.
            "-nic", qemu_ports.qemu_nic_arg(),
            "-nographic",
            "-serial", "mon:stdio",
            *qemu_ports.qemu_serial_args(),  # UART1/UART2 (RS485-1/2) TCP chardevs
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
    # The tree lock is NOT released here, deliberately. Session-fixture teardown runs before
    # the --junitxml report is written and before pytest_sessionfinish reads
    # build/qemu_test.log to dump it — two of the artifacts this lock protects. It is
    # released in pytest_unconfigure, which runs after both.


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
# WBMGEAPI's 30 s default (api_client.py:85/:89).
#
# pytest-timeout charges setup + call + teardown of an item to ONE budget, and a
# module-scoped fixture is torn down inside the LAST item of its module — so the restore
# POSTs below land in some ordinary test's timeout budget, and several modules run
# per-test budgets of 30 s or less. Inheriting 30 s would let a single stalled request
# eat a whole item budget on its own and report as "that test timed out", with nothing
# pointing at conftest.
#
# A (connect, read) TUPLE, not a scalar. In requests a scalar timeout applies to EACH phase
# separately, and _DelayedSession sends Connection: close (api_client.py:37, :59) so every call
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
# Resulting ceilings. The TEARDOWN figure is the one the 15 items listed at the end of
# _restore_rs485_settings quote in their @pytest.mark.timeout comments:
#   per call : 0.1 s (_DelayedSession.DELAY_S) + 5 s connect + 15 s read = 20.1 s
#   teardown : 2 ports x 20.1 s + _RS485_RESTORE_SETTLE_S = 41.2 s  ("45 s allowance")
# The PER-CALL figure is not bound to that enumeration — it is quoted more widely. Six of
# those files cite it a second time, on its own, in the marker of their FIRST item, for the
# once-per-session rs485 snapshot that is charged there when the file is run alone:
# 28_:77, 29_:221, 31_:221, 38_:200, 39_:325, 49_:105. Nor does every listed file quote both:
# 40_:120-123 quotes the 41.2 s teardown ceiling and never the per-call one. Changing either
# number means re-checking both sets.
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
        # to 61.3 s, past the 45 s allowance quoted by these 15 items:
        # 20/21/24/25/27/28/29/31/35/38/39/40/41/42/49. It is not a suite-wide invariant and
        # must not be stated as one — 03_ decomposes its budget without any such allowance
        # (42 calls x 10 s + delays), and 36_/37_ use a single undecomposed, deliberately
        # generous number instead. 1 s is the same settle window the suite already uses for a port
        # rebind (e.g. 20/31's cache fixtures) and comfortably covers the "few hundred
        # milliseconds" the release->acquire window is documented to take
        # (settings_update.c:231-244).
        time.sleep(_RS485_RESTORE_SETTLE_S)


# How many EARLIER tests _uart_leak_guard names when it warns, on top of the test whose
# teardown fired the probe. It cannot name one culprit (see its docstring: the listen
# backlog hides the leaker's own teardown), so it hands over a short window instead.
# Three covers the observed one-test lag with room to spare and still fits in a message
# a human will actually read.
_UART_LEAK_GUARD_WINDOW = 3


@pytest.fixture(scope="session")
def _uart_leak_state():
    """Session state for _uart_leak_guard: the recent test ids, and the warned ports.

    `warned` is a SET OF PORTS, not one boolean. UART1 and UART2 are two independent
    single-client resources, and one shared flag broke in two ways: a leak on UART2 that
    began while UART1 was legitimately held (20_test_cache_tcp_framing's module-scoped
    cache_tcp_server holds UART1 across all six of its tests) was never reported, and —
    worse — once one port stayed wedged, the "both ports accept again" re-arm condition
    could never hold again, so the flag stayed set and the guard went mute for the rest
    of the session: exactly the failure the per-episode re-arm was introduced to
    prevent, reached through the other port. Per port, each port's episode is
    independent and a wedged UART2 cannot silence reporting on UART1.

    A session-scoped fixture rather than an attribute stashed on request.config: this is
    state that belongs to one guard for the length of one session, pytest already owns
    exactly that lifetime, and the dependency shows up in the fixture graph instead of
    being an undeclared attribute appearing on a shared Config object.
    """
    return {"recent": [], "warned": set()}


@pytest.fixture(autouse=True)
def _uart_leak_guard(request, _uart_leak_state):
    """Diagnostic-only: warn when a QEMU UART chardev stops accepting connections.

    QEMU exposes each RS-485 UART as a SINGLE-CLIENT TCP chardev (this slot's UART1 and
    UART2 ports, qemu_ports.UART1_TCP_PORT / UART2_TCP_PORT), so one
    leaked socket — typically a daemon helper thread whose stop() an exception skipped —
    keeps the single accept slot and every later test that needs that UART fails. Those
    downstream failures all name the wrong test, so this guard probes both ports after
    each test and reports what it saw, to give the reader somewhere to start.

    It WARNS and must NOT fail. Not out of a general caution about "fragile suites" —
    that was the old reasoning here, and being a conclusion with no argument attached it
    is exactly what let the two defects below sit in this fixture unnoticed. Both are
    properties of the probe itself, and neither is fixed by making the probe angrier:

    1. It cannot tell a leak from a legitimate long-lived owner. The guard is
       function-scoped; the sockets it probes are not. 20_test_cache_tcp_framing's
       module-scoped `cache_tcp_server` fixture holds a PacketInjector on UART1 across all
       six tests of that module, so from the second test onward the probe finds UART1
       unreachable in a perfectly healthy run. (29_test_gateway_dual_port's module-scoped
       `dual_gateway_slave` holds both chardevs the same way, and escapes notice only
       because that module contains exactly one test.) A failing guard would therefore
       fail correct runs; a warning one was harmless only because nobody noticed it
       firing.

    2. It cannot attribute. QEMU's LISTENING socket stays open while its one client slot
       is occupied, so the kernel completes the first connect() after a leak out of the
       backlog and the probe SUCCEEDS. In the teardown of the test that actually leaked
       the guard thus sees a reachable port and says nothing; the next test's teardown is
       the one that times out. (Measured against a plain listen(1) socket that never
       calls accept(): the first connect succeeds, the second and later time out.)
       Whatever request.node.nodeid holds when this fires is therefore usually not the
       culprit — which is why the message hands over a window of recent tests and says
       the leak is in or shortly before them, rather than accusing one test.

    Doing this properly needs ownership data the probe does not have: a registry of who
    opened each chardev, in which wider-scope fixtures mark their sockets long-lived.
    Until that exists this fixture reports an observation, never a verdict.

    Warns at most once per PORT per EPISODE, and each port is tracked on its own (see
    _uart_leak_state for what a single shared flag got wrong): a port is re-armed the
    moment it accepts again, a warning names only the ports NEWLY seen unreachable, and
    a port that is still wedged from an earlier warning stays quiet. A once-per-session
    flag (the first version of this) would be spent by module 20's legitimate holder on
    nearly every run — that module starts about a third of the way into the run (74 of
    229 collected items precede it) — leaving the guard mute for the rest of the suite.
    Edge-triggering still suppresses the wall of identical warnings a wedged port would
    otherwise produce, without trading away every later report. It keeps probing while
    suppressed, since that is how it notices the port coming back: on a genuinely wedged
    port that costs one connect timeout per remaining test, exactly as it did before.

    Active whenever the chardev is this run's own — --qemu, or an --ip pointing at a QEMU
    on this machine (_uart_chardev_is_ours). The second half matters: `pytest --ip
    localhost:<web port>` with no --qemu is the documented way to drive an already-running
    QEMU, and a loopback endpoint is the --ip default (it follows the port slot), so gating
    on --qemu alone left the guard mute in
    the ordinary local run. Against a REMOTE --ip there is no chardev on this host to
    probe, and this slot's two UART ports here belong to whatever else happens to be
    listening, so the
    guard stays off.
    """
    yield
    # EVERYTHING below is wrapped, because this fixture must never break a run: an
    # unwrapped warnings.warn() raises the warning under a `filterwarnings = error` this
    # suite may adopt later, and nothing else here is worth an ERROR on an innocent
    # test's teardown. A silent guard is merely useless; a guard that raises is worse
    # than no guard. The wrapper is a BACKSTOP, not the plan: the one failure we can
    # name — socket.socket() raising OSError: [Errno 24] Too many open files under the
    # very descriptor leak this guard looks for — is handled per port inside the loop,
    # because reaching this handler means losing the probe of the OTHER port and the
    # warning with it. This handler must therefore stay loud (it prints) rather than
    # swallow silently: anything landing here is unexpected by construction.
    try:
        if not _uart_chardev_is_ours(config=request.config):
            return
        recent = _uart_leak_state["recent"]
        # Snapshot BEFORE appending: `predecessors` is the tests that ran BEFORE this
        # one, so the message names _UART_LEAK_GUARD_WINDOW of them plus the current
        # test, instead of spending the first window slot repeating the test it has
        # already named in "after {nodeid}".
        predecessors = list(recent)
        recent.append(request.node.nodeid)
        # Trim by COUNT, not by the negative-slice `del recent[:-WINDOW]`: that form is
        # correct for WINDOW >= 1 but silently becomes `del recent[:0]` — a no-op that
        # lets `recent` grow for the whole session — the moment someone sets the constant
        # to 0 to turn the window off.
        del recent[:max(0, len(recent) - _UART_LEAK_GUARD_WINDOW)]
        unreachable = []
        for port in (qemu_ports.UART1_TCP_PORT, qemu_ports.UART2_TCP_PORT):
            # socket() and settimeout() belong INSIDE this try. Under the descriptor leak
            # this guard exists to point at, socket() itself raises EMFILE; from outside
            # the try that escaped to the outer handler, so the second port was never
            # probed and the guard reported nothing in exactly its headline scenario.
            probe = None
            try:
                probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                probe.settimeout(2.0)
                probe.connect((UART_CHARDEV_HOST, port))
            except (ConnectionRefusedError, OSError, socket.timeout) as exc:
                if probe is None:
                    # socket() failed: we never asked the port anything, so calling it
                    # unreachable would be a lie. Say what happened and move on to the
                    # other port — an EMFILE here is itself the leak symptom.
                    print(f"UART leak guard: could not create a probe socket for port "
                          f"{port} ({exc!r}); that port was NOT probed. An 'Errno 24 Too "
                          f"many open files' here is the descriptor leak this guard looks "
                          f"for, seen from the probe side.")
                else:
                    unreachable.append(port)
            finally:
                if probe is not None:
                    try:
                        probe.close()
                    except OSError:
                        pass
        warned = _uart_leak_state["warned"]
        # Re-arm every port that accepts again — whatever held it (a leak, or a
        # wider-scope fixture that has since torn down) is gone. Per port and not "all
        # ports free", so one wedged port cannot keep the other suppressed.
        warned.intersection_update(unreachable)
        newly = [port for port in unreachable if port not in warned]
        if not newly:
            return
        # Ports still wedged from an EARLIER warning. Named in the message even though
        # they are not re-reported: without them, the "UART2 wedged, then UART1 wedges too"
        # state printed only UART1's port and the closing "the other port is unaffected"
        # read as a claim that UART2 was fine. Computed before warned.update(newly).
        still = [port for port in unreachable if port in warned]
        still_note = (f" Port(s) {still} were already reported unreachable earlier and "
                      f"are STILL unreachable (not re-reported here)." if still else "")
        warned.update(newly)
        window = ", ".join(reversed(predecessors)) or "(none: first test of the session)"
        warnings.warn(
            f"{UART_CHARDEV_LEAK_MARKER}: port(s) {newly} did not take a probe "
            f"connection after {request.node.nodeid}. If a UART socket was leaked, it "
            f"happened in that test or shortly before it — most recent first, the tests "
            f"that ran before it are: {window}. This is a hint, not a verdict: the probe "
            f"cannot distinguish a leak "
            f"from a chardev legitimately held by a live module-scoped fixture (e.g. "
            f"cache_tcp_server in 20_test_cache_tcp_framing.py holds UART1 "
            f"({qemu_ports.UART1_TCP_PORT}) for its whole "
            f"module), and QEMU's listen backlog absorbs the first connect after a leak, "
            f"so this warning lags the leak by roughly one test. Ignore it when one of "
            f"the tests above is meant to be holding the chardev; otherwise look in them "
            f"for a helper thread whose stop() an exception skipped. Port(s) {newly} "
            f"stay suppressed until they accept again; every port is tracked "
            f"separately.{still_note}",
            stacklevel=1,
        )
    except Exception as exc:  # noqa: BLE001 - diagnostics must never break a run
        # print, not warnings.warn/raise: this handler is the last line of the promise
        # that the guard never breaks a run, and warn() would raise under a future
        # `filterwarnings = error`. A bare `pass` here used to make the guard vanish
        # without trace exactly when something unexpected was happening; the suite runs
        # with -s (pytest.ini addopts), so this line reaches the log.
        print(f"UART leak guard: skipped this teardown after an unexpected error: "
              f"{exc!r}. Chardev leak diagnostics are incomplete for this run.")
