"""Reboot test — must run last"""

import time
import pytest


@pytest.mark.timeout(120)
def test_reboot(api):
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

    custom_settings = {
        "hostname": "persist-test-host",
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
        pytest.fail("Device did not come back within 30 seconds after reboot")
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
    assert new_uptime_s < original_uptime_s, \
        f"Uptime after reboot ({new_uptime_s}s) >= uptime before reboot ({original_uptime_s}s) — no reboot detected"
    print("✓ Uptime correctly reset after reboot")

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
