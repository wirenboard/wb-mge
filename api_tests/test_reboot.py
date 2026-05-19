"""Reboot tests — must run last"""

import time
import pytest


@pytest.mark.timeout(120)
@pytest.mark.order(34)
def test_settings_persist_after_reboot(api):
    """Write custom settings, reboot, verify they survived"""
    response = api.get_settings()
    assert response.status_code == 200
    original = response.json()

    custom_settings = {
        "hostname": "persist-test-host",
        "vout": not original["vout"],
        "io_bus": not original["io_bus"],
        "rs485_1": {
            "baudrate": 38400 if original["rs485_1"]["baudrate"] != 38400 else 19200,
            "term": not original["rs485_1"]["term"],
            "fail_safe": not original["rs485_1"]["fail_safe"],
        },
        "rs485_2": {
            "stopbits": "2" if original["rs485_2"]["stopbits"] != "2" else "1",
            "parity": "odd" if original["rs485_2"]["parity"] != "odd" else "even",
        },
    }

    response = api.update_settings(custom_settings)
    assert response.status_code == 200
    assert response.json().get("success") == True
    print("✓ Custom settings written before reboot")

    response = api.get_settings()
    assert response.status_code == 200
    pre_reboot = response.json()
    assert pre_reboot["hostname"] == custom_settings["hostname"]
    assert pre_reboot["vout"] == custom_settings["vout"]
    print("✓ Custom settings confirmed via read-back (pre-reboot)")

    time.sleep(2)

    try:
        response = api.execute_command("reboot")
        print(f"  Reboot command status: {response.status_code}")
        assert response.status_code == 200
    except ConnectionError:
        print("  Connection dropped (expected during reboot)")

    print("  Waiting for device to reboot...")
    try:
        api.wait_for_ready(timeout=30)
    except TimeoutError:
        pytest.fail("Device did not come back within 30 seconds after reboot")
    print("✓ Device came back online after reboot")

    response = api.get_settings()
    assert response.status_code == 200
    post = response.json()

    assert post["hostname"] == custom_settings["hostname"], \
        f"hostname not persisted: expected {custom_settings['hostname']!r}, got {post['hostname']!r}"
    assert post["vout"] == custom_settings["vout"], \
        f"vout not persisted: expected {custom_settings['vout']}, got {post['vout']}"
    assert post["io_bus"] == custom_settings["io_bus"], \
        f"io_bus not persisted: expected {custom_settings['io_bus']}, got {post['io_bus']}"
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

    reset = api.execute_command("set_default_settings")
    assert reset.status_code == 200
    time.sleep(2)
    print("✓ Settings reset to defaults")


@pytest.mark.timeout(120)
@pytest.mark.order(35)
def test_reboot_command(api):
    """Test POST /cmd reboot — verify device reboots and uptime resets"""
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

    # Reset settings to defaults before rebooting so the device always boots
    # with known credentials (admin/admin), regardless of what previous tests changed.
    reset_response = api.execute_command("set_default_settings")
    assert reset_response.status_code == 200, \
        f"set_default_settings failed (HTTP {reset_response.status_code}): {reset_response.text}"
    assert reset_response.json().get("success") == True, \
        f"set_default_settings returned success=false: {reset_response.text}"
    time.sleep(2)  # Allow the settings update task to finish writing to NVS

    try:
        response = api.execute_command("reboot")
        print(f"  Reboot command status: {response.status_code}")
        assert response.status_code == 200, \
            f"POST /cmd reboot expected 200, got {response.status_code}"
    except ConnectionError:
        print("  Connection dropped (expected during reboot)")

    print("  Waiting for device to reboot...")
    try:
        api.wait_for_ready(timeout=30)
    except TimeoutError:
        pytest.fail("Device did not come back within 30 seconds after reboot command")
    print("✓ Device came back online and re-authenticated successfully")

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
    assert new_uptime_s < original_uptime_s, \
        f"Uptime after reboot ({new_uptime_s}s) >= uptime before reboot ({original_uptime_s}s) — no reboot detected"
    print("✓ Uptime correctly reset after reboot")
