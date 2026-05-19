"""Reboot command test — must run last"""

import time
import pytest


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
