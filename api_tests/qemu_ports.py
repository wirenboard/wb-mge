"""Single source of truth for the host-side QEMU ports.

Every host port the test suite touches — the QEMU hostfwd targets, the two UART
chardev TCP ports, and the UDP IO-bus port — is derived here from ONE integer
"slot" so that N independent full runs can coexist on one host without fighting
over the same device under test. Guest-side ports never change (they live inside
each QEMU); only the host side moves.

Offset source: the WB_MGE_PORT_SLOT environment variable (integer >= 0, default
0). Rationale for env-var over the alternatives:
  * Reproducible — the same slot always yields the same ports, unlike a pid.
  * Explicitly controllable — a parallel launcher sets one env var per instance.
  * No bind(0) race — allocating a free port with bind(0) then closing it leaves
    a window in which another process (or the sibling QEMU) can grab the just-
    released port before our QEMU binds it. A fixed slot has no such window.
  * PYTEST_XDIST_WORKER is honored as a FALLBACK (gw0 -> 0, gw1 -> 1, ...) when
    WB_MGE_PORT_SLOT is unset, so the same scheme also serves `pytest -n`.

Layout: a slot maps to a CONTIGUOUS block of ports, NOT an offset added to each
legacy base port. The legacy base ports sit only one apart (8080/8081,
50502/50503/50504, 5561/5562), so adding a small per-instance offset to each
would alias one instance's port onto another's. A contiguous block per slot
cannot alias.

    block_start = _BLOCK_BASE + slot * _BLOCK_SIZE

_BLOCK_BASE is 21000 and _BLOCK_SIZE is 16 (> the 8 ports assigned per block), so
the ports for the slots actually run (0..~10) land in 21000..21175 — comfortably
below the Linux ephemeral range (default 32768+), so the OS will not hand a
random client socket one of our reserved numbers. Slots up to ~2700 keep every
port < 65536; _slot_from_env() rejects anything larger.
"""
import os

_BLOCK_BASE = 21000
_BLOCK_SIZE = 16
_PORTS_PER_SLOT = 8  # logical ports assigned per block (indices 0..7)
_MAX_SLOT = (65535 - _BLOCK_BASE) // _BLOCK_SIZE - 1


def _slot_from_env() -> int:
    """Resolve this run's slot from the environment (see module docstring)."""
    raw = os.environ.get("WB_MGE_PORT_SLOT")
    if raw is None:
        worker = os.environ.get("PYTEST_XDIST_WORKER", "")
        if worker.startswith("gw") and worker[2:].isdigit():
            raw = worker[2:]
        else:
            raw = "0"
    try:
        slot = int(raw)
    except ValueError:
        raise ValueError(f"WB_MGE_PORT_SLOT must be an integer, got {raw!r}")
    if slot < 0 or slot > _MAX_SLOT:
        raise ValueError(
            f"WB_MGE_PORT_SLOT out of range: {slot} (allowed 0..{_MAX_SLOT})"
        )
    return slot


SLOT = _slot_from_env()
_BLOCK_START = _BLOCK_BASE + SLOT * _BLOCK_SIZE

HOST = "127.0.0.1"
GATEWAY_HOST = HOST  # alias used across the test modules

# Host ports, one per index within this slot's block. The guest port each one
# forwards to is noted in the comment (guest ports are fixed inside QEMU).
HTTP_HOST_PORT = _BLOCK_START + 0                 # guest 80    (web API)
ALT_WEB_HOST_PORT = _BLOCK_START + 1              # guest 8081  (alt web-port test)
GATEWAY_HOST_PORT = _BLOCK_START + 2              # guest 502   (Modbus gateway)
TRANSPARENT_PORT2_HOST_PORT = _BLOCK_START + 3    # guest 503   (transparent bridge, port 2)
# Guest 50504 is shared by the cache Modbus server AND transparent bridge port 1,
# so all these names alias one host port.
CACHE_MODBUS_HOST_PORT = _BLOCK_START + 4         # guest 50504
TRANSPARENT_PORT1_HOST_PORT = CACHE_MODBUS_HOST_PORT
TRANSPARENT_HOST_PORT = CACHE_MODBUS_HOST_PORT    # 25_'s name for the same port
QEMU_CACHE_MODBUS_PORT = CACHE_MODBUS_HOST_PORT   # 20_/31_/37_/42_/43_'s name
MODBUS_TCP_HOST_PORT = GATEWAY_HOST_PORT          # 26_'s name for the gateway port
IO_BUS_UDP_PORT = _BLOCK_START + 5                # guest 5570  (UDP IO state bus)
UART1_TCP_PORT = _BLOCK_START + 6                 # QEMU -serial chardev, UART1 (RS485-1)
UART2_TCP_PORT = _BLOCK_START + 7                 # QEMU -serial chardev, UART2 (RS485-2)

# Web-port test (40_) names: the "default" web port is the HTTP port; the "alt"
# is the alternate. Guest side stays 80 / 8081; only host moves.
DEFAULT_PORT_HOST = HTTP_HOST_PORT
ALT_PORT_HOST = ALT_WEB_HOST_PORT
ALT_PORT_GUEST = 8081  # guest port unchanged

# Fixed guest ports (the '-:<N>' side of each hostfwd). Kept here so the QEMU arg
# builder and the port map live in one file.
_GUEST_HTTP = 80
_GUEST_ALT_WEB = 8081
_GUEST_GATEWAY = 502
_GUEST_TRANSPARENT_P2 = 503
_GUEST_CACHE_MODBUS = 50504
_GUEST_IO_BUS = 5570

# The full set of host ports THIS run reserves. Used by the stale-port preflight
# so it checks ONLY its own ports and never false-positives on a sibling run.
MY_HOST_PORTS = [
    HTTP_HOST_PORT,
    ALT_WEB_HOST_PORT,
    GATEWAY_HOST_PORT,
    TRANSPARENT_PORT2_HOST_PORT,
    CACHE_MODBUS_HOST_PORT,
    IO_BUS_UDP_PORT,
    UART1_TCP_PORT,
    UART2_TCP_PORT,
]


def qemu_nic_arg() -> str:
    """The '-nic user,...' value with hostfwd rules pointing at this slot's host ports."""
    return (
        "user,model=open_eth,"
        f"hostfwd=tcp:{HOST}:{HTTP_HOST_PORT}-:{_GUEST_HTTP},"
        f"hostfwd=tcp:{HOST}:{ALT_WEB_HOST_PORT}-:{_GUEST_ALT_WEB},"
        f"hostfwd=tcp:{HOST}:{GATEWAY_HOST_PORT}-:{_GUEST_GATEWAY},"
        f"hostfwd=tcp:{HOST}:{TRANSPARENT_PORT2_HOST_PORT}-:{_GUEST_TRANSPARENT_P2},"
        f"hostfwd=tcp:{HOST}:{CACHE_MODBUS_HOST_PORT}-:{_GUEST_CACHE_MODBUS},"
        f"hostfwd=udp:{HOST}:{IO_BUS_UDP_PORT}-:{_GUEST_IO_BUS}"
    )


def qemu_serial_args() -> list:
    """The two -serial UART chardev args (placed after '-serial mon:stdio')."""
    return [
        "-serial", f"tcp::{UART1_TCP_PORT},server,nowait",
        "-serial", f"tcp::{UART2_TCP_PORT},server,nowait",
    ]
