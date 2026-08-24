"""Reboot test — must run last"""

import os
import time
import warnings

import pytest
import requests


def _serial_log_path():
    """Path to the live QEMU serial capture written by conftest (build/qemu_test.log)."""
    return os.path.join(os.path.dirname(__file__), "..", "build", "qemu_test.log")


def _serial_log_offset():
    """Size of the QEMU serial log right now, or None if it cannot be read.

    Captured before the reboot so the scan below only sees serial from THIS reboot. In a
    full run the log holds no earlier marker anyway — conftest truncates it at QEMU start
    and this is the first of the deferred reboot tests — so the offset is insurance rather
    than today's load-bearing detail: under a partial selection that runs another reboot
    test first, a scan from 0 would match that one and pass unconditionally.
    """
    try:
        return os.path.getsize(_serial_log_path())
    except OSError:
        return None


# reboot_task logs _MARKER_EXECUTED as its very first statement, so that line proves the
# task actually ran. cmd_reboot_device logs _MARKER_SCHEDULED before xTaskCreate, so it
# proves only that the command ARRIVED — when the task cannot be created it says so and the
# handler still answers 200.
_MARKER_EXECUTED = "Executing reboot command"
_MARKER_SCHEDULED = "Scheduling device reboot"


def _reboot_serial_evidence(offset, read_timeout=10.0):
    """What serial written since `offset` says about the reboot command.

    "executed"  — reboot_task ran, so the reset was ours.
    "scheduled" — the command arrived but the task was never created. cmd_reboot_device
                  logs that and still returns 200, so this is the signature of heap
                  exhaustion, NOT of a request lost on the way to the guest.
    "absent"    — no trace of the command at all; it never reached the guest.
    None        — the log could not be read, or was truncated under us. This must NOT
                  collapse into "absent": an unreadable file says nothing about the
                  firmware, and the caller's assertion accuses it. Same reason 33_'s
                  _sessions_restored_after_reboot reports "unknown".

    Retried rather than read once: QEMU writes this log from a separate process through
    its own stdio buffering, so the line can lag the device answering HTTP again.
    """
    deadline = time.monotonic() + read_timeout
    readable, tail = False, ""
    while True:
        try:
            if os.path.getsize(_serial_log_path()) < offset:
                return None  # truncated under us — the offset no longer means anything
            with open(_serial_log_path(), "r", errors="replace") as fh:
                fh.seek(offset)
                tail = fh.read()
            readable = True
            if _MARKER_EXECUTED in tail:
                return "executed"
        except OSError:
            pass
        if time.monotonic() >= deadline:
            if not readable:
                return None
            return "scheduled" if _MARKER_SCHEDULED in tail else "absent"
        time.sleep(0.5)


@pytest.mark.timeout(2400)
def test_reboot(api, request):
    """Reboot: verify uptime resets and custom settings survive"""
    # Allow previous tests' teardown activity to settle before making requests
    time.sleep(1)
    # --- remember uptime before reboot ---
    response = api.get_uptime()
    assert response.status_code == 200
    uptime_data = response.json()
    original_uptime_s = (
        uptime_data["days"] * 86400
        + uptime_data["hours"] * 3600
        + uptime_data["minutes"] * 60
        + uptime_data["seconds"]
    )
    print(f"  Uptime before reboot: {original_uptime_s}s "
          f"({uptime_data['days']}d {uptime_data['hours']}h "
          f"{uptime_data['minutes']}m {uptime_data['seconds']}s)")

    # --- reset to defaults so previous tests' network changes don't break connectivity ---
    reset_response = api.execute_command("set_default_settings")
    assert reset_response.status_code == 200
    assert reset_response.json().get("success") == True
    time.sleep(2)
    print("✓ Settings reset to defaults before applying custom values")

    # --- read defaults, then apply custom non-network settings ---
    response = api.get_settings()
    assert response.status_code == 200
    defaults = response.json()

    assert defaults["update_channel"] == "stable", \
        f"update_channel must default to 'stable', got {defaults['update_channel']!r}"

    custom_settings = {
        "hostname": "persist-test-host",
        "update_channel": "testing",
        "vout": not defaults["vout"],
        "io_bus": not defaults["io_bus"],
        "rs485_1": {
            "baudrate": 38400 if defaults["rs485_1"]["baudrate"] != 38400 else 19200,
            "term": not defaults["rs485_1"]["term"],
            "fail_safe": not defaults["rs485_1"]["fail_safe"],
        },
        "rs485_2": {
            "stopbits": "2" if defaults["rs485_2"]["stopbits"] != "2" else "1",
            "parity": "odd" if defaults["rs485_2"]["parity"] != "odd" else "even",
        },
    }

    response = api.update_settings(custom_settings)
    assert response.status_code == 200
    assert response.json().get("success") == True
    print("✓ Custom settings written")

    response = api.get_settings()
    assert response.status_code == 200
    pre_reboot = response.json()
    assert pre_reboot["hostname"] == custom_settings["hostname"]
    assert pre_reboot["vout"] == custom_settings["vout"]
    print("✓ Custom settings confirmed via read-back (pre-reboot)")

    time.sleep(2)

    # --- reboot ---
    # Snapshot the serial log before the POST; the reset-origin check after the reboot scans
    # only what was written past this point.
    serial_offset = _serial_log_offset() if request.config.getoption("--qemu") else None

    # A missing reply is not evidence the reboot failed. reboot_task (cmd_handler.c) gives
    # httpd 100 ms to flush the 200 before esp_restart() in the QEMU build — hardware waits
    # REBOOT_DELAY_MS = 1000, so this narrow window is a property of the test build, not a
    # shipped defect — and under CI node contention httpd can miss it, leaving the chip to
    # vanish mid-response. Under slirp hostfwd that surfaces as a ReadTimeout essentially
    # always: slirp completes the host-side connect itself regardless of guest state (see
    # 13_test_ports.py), so a dead guest cannot produce a connect error, and a requests
    # ConnectionError here would mean QEMU itself died. Note the qualified names — requests'
    # ConnectionError is a *sibling* of the builtin, not a subclass, so a bare
    # `except ConnectionError` catches none of this.
    # Not retryable: POST /cmd reboot is not idempotent and a second one would reset the
    # device mid-verification. The reboot is confirmed below instead — wait_for_ready() that
    # it came back, uptime that it rebooted, serial that the reset was our command.
    try:
        response = api.execute_command("reboot")
    except requests.exceptions.RequestException as exc:
        # warn(), not print(): -s output reaches neither qemu_test_report.xml nor the report
        # sections, so on a green build a print here is invisible. Carry the exception TEXT
        # too — it is what separates "read timed out" (the guest vanished, expected) from a
        # connect error (QEMU died, a different diagnosis).
        warnings.warn(
            f"test_reboot: no reply to POST /cmd reboot ({type(exc).__name__}: {exc}); "
            f"treating the device as resetting — confirmed below by uptime "
            f"(and, under --qemu, by serial).",
            stacklevel=1,
        )
    else:
        print(f"  Reboot command status: {response.status_code}")
        assert response.status_code == 200, \
            f"POST /cmd reboot expected 200, got {response.status_code}"

    print("  Waiting for device to reboot...")
    try:
        api.wait_for_ready(timeout=1800)
    except TimeoutError:
        pytest.fail("Device did not come back within 1800 seconds after reboot")
    print("✓ Device came back online")

    # --- verify uptime reset ---
    response = api.get_uptime()
    assert response.status_code == 200
    new_uptime_data = response.json()
    new_uptime_s = (
        new_uptime_data["days"] * 86400
        + new_uptime_data["hours"] * 3600
        + new_uptime_data["minutes"] * 60
        + new_uptime_data["seconds"]
    )
    print(f"  Uptime after reboot: {new_uptime_s}s")

    # Collect the serial evidence BEFORE asserting on uptime. When the POST never reached the
    # guest at all — a loaded node can drop it, and under slirp that is indistinguishable
    # from a lost reply — the uptime assert is the one that fires, and the marker is exactly
    # what separates "the command never arrived" from "the device ignored it".
    evidence = _reboot_serial_evidence(serial_offset) if serial_offset is not None else None
    evidence_note = {
        "executed": " (serial: the reboot command was executed)",
        "scheduled": " (serial: the command ARRIVED but the reboot task was never created — "
                     "cmd_reboot_device logged the failure and still answered 200; this is a "
                     "heap regression, not a lost request)",
        "absent": " (serial: no trace of the command — it never reached the guest)",
    }.get(evidence, "")

    assert new_uptime_s < original_uptime_s, (
        f"Uptime after reboot ({new_uptime_s}s) >= uptime before reboot "
        f"({original_uptime_s}s) — no reboot detected{evidence_note}"
    )
    print("✓ Uptime correctly reset after reboot")

    # Uptime proves *a* reset happened, not that OUR command caused it. A panic or WDT inside
    # the POST /cmd handler has the identical signature — no reply, device returns, uptime
    # reset, NVS intact — so tolerating the missing reply above would let that defect pass
    # silently. reboot_task logs this line only when cmd_reboot_device() created it, so
    # finding it in serial written since the snapshot separates the two.
    if serial_offset is not None:
        if evidence is None:
            # Log unreadable or truncated — infrastructure, not evidence about the firmware.
            warnings.warn(
                "test_reboot: build/qemu_test.log became unreadable, or was truncated by a "
                "second run in this tree, so the reset-origin check was skipped; a panic in "
                "the /cmd handler would go unnoticed.",
                stacklevel=1,
            )
        else:
            assert evidence == "executed", (
                f"device reset, but {_MARKER_EXECUTED!r} never reached QEMU serial in the "
                f"10 s after it came back — the reset was not the commanded reboot (a panic "
                f"or WDT inside the /cmd handler looks like this){evidence_note}"
            )
            print("✓ Serial confirms the reset came from the reboot command")
    elif request.config.getoption("--qemu"):
        warnings.warn(
            "test_reboot: build/qemu_test.log could not be read before the reboot, so the "
            "reset-origin check was skipped; a panic in the /cmd handler would go unnoticed.",
            stacklevel=1,
        )

    # --- verify settings persisted ---
    response = api.get_settings()
    assert response.status_code == 200
    post = response.json()

    assert post["hostname"] == custom_settings["hostname"], \
        f"hostname not persisted: expected {custom_settings['hostname']!r}, got {post['hostname']!r}"
    assert post["vout"] == custom_settings["vout"], \
        f"vout not persisted: expected {custom_settings['vout']}, got {post['vout']}"
    assert post["io_bus"] == custom_settings["io_bus"], \
        f"io_bus not persisted: expected {custom_settings['io_bus']}, got {post['io_bus']}"
    assert post["update_channel"] == custom_settings["update_channel"], \
        f"update_channel not persisted: expected {custom_settings['update_channel']!r}, got {post['update_channel']!r}"
    assert post["rs485_1"]["baudrate"] == custom_settings["rs485_1"]["baudrate"], \
        f"rs485_1.baudrate not persisted: expected {custom_settings['rs485_1']['baudrate']}, got {post['rs485_1']['baudrate']}"
    assert post["rs485_1"]["term"] == custom_settings["rs485_1"]["term"], \
        f"rs485_1.term not persisted: expected {custom_settings['rs485_1']['term']}, got {post['rs485_1']['term']}"
    assert post["rs485_1"]["fail_safe"] == custom_settings["rs485_1"]["fail_safe"], \
        f"rs485_1.fail_safe not persisted: expected {custom_settings['rs485_1']['fail_safe']}, got {post['rs485_1']['fail_safe']}"
    assert post["rs485_2"]["stopbits"] == custom_settings["rs485_2"]["stopbits"], \
        f"rs485_2.stopbits not persisted: expected {custom_settings['rs485_2']['stopbits']!r}, got {post['rs485_2']['stopbits']!r}"
    assert post["rs485_2"]["parity"] == custom_settings["rs485_2"]["parity"], \
        f"rs485_2.parity not persisted: expected {custom_settings['rs485_2']['parity']!r}, got {post['rs485_2']['parity']!r}"
    print("✓ All custom settings persisted after reboot")

    # --- restore defaults ---
    reset = api.execute_command("set_default_settings")
    assert reset.status_code == 200
    time.sleep(2)
    print("✓ Settings reset to defaults")
