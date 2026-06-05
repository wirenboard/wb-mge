"""
Tests for the wifi_perm_disable setting.

Verifies that:
- wifi_perm_disable defaults to false and the wifi group is present
- POSTing wifi_perm_disable=false is a no-op on hardware (QEMU: allowed for teardown)
- POSTing wifi_perm_disable=true permanently disables WiFi, persists across reboots
- After activation, /info reflects wifi.enabled=false and wifi.perm_disabled=true
- wifi_scan/start is rejected while wifi is permanently disabled

The module fixture clean_wifi_perm_disable resets the flag via QEMU-only path
after all tests complete, leaving the flash image clean for subsequent test runs.
"""

import pytest


@pytest.fixture(scope="module", autouse=True)
def clean_wifi_perm_disable(api):
    """Module fixture: ensure wifi_perm_disable is reset after all tests in this module.

    setup  — nothing (flag is false by default).
    teardown — POST wifi_perm_disable=false (QEMU-only: allowed in QEMU builds),
               then reboot to restore initial state.
    """
    yield  # run all tests in the module

    # Teardown: clear the flag and reboot so the flash image is left clean
    try:
        api.update_settings({"wifi_perm_disable": False})
    except Exception as exc:
        print(f"  teardown: POST wifi_perm_disable=false failed: {exc}")
        return

    try:
        api.execute_command("reboot")
    except Exception:
        pass  # connection drop on reboot is expected

    try:
        api.wait_for_ready(timeout=1800)
        print("✓ teardown: wifi_perm_disable cleared and device rebooted to clean state")
    except TimeoutError:
        print("✗ teardown: device did not come back after teardown reboot")


def test_wifi_perm_disable_default_is_false(api):
    """GET /settings — wifi_perm_disable is false by default and wifi group is present."""
    response = api.get_settings()
    assert response.status_code == 200, f"GET /settings failed: {response.status_code}"

    data = response.json()
    # wifi_perm_disable is either absent (implying false) or explicitly false
    assert data.get("wifi_perm_disable", False) is False, (
        f"wifi_perm_disable should be false by default, got: {data.get('wifi_perm_disable')}"
    )
    assert "wifi" in data, "wifi group must be present when wifi_perm_disable is false"
    print("✓ wifi_perm_disable defaults to false and wifi group is present")


def test_wifi_perm_disable_false_is_ignored(api):
    """POST /settings with wifi_perm_disable=false must be silently ignored."""
    response = api.update_settings({"wifi_perm_disable": False})
    assert response.status_code == 200, f"POST /settings failed: {response.status_code}"
    assert response.json().get("success") is True, (
        f"Expected success=true, got: {response.json()}"
    )
    print("✓ POST wifi_perm_disable=false returned success=true")

    # Verify that the wifi group is still present after the no-op POST
    response = api.get_settings()
    assert response.status_code == 200, f"GET /settings failed: {response.status_code}"
    data = response.json()
    assert data.get("wifi_perm_disable", False) is False, (
        "wifi_perm_disable must remain false after posting false"
    )
    assert "wifi" in data, "wifi group must still be present after posting wifi_perm_disable=false"
    print("✓ wifi group still present; wifi_perm_disable is still false")


@pytest.mark.timeout(2400)
def test_wifi_perm_disable_activation(api):
    """
    Activate wifi_perm_disable=true, reboot, and verify the setting is permanent.

    Steps:
      1. POST wifi_perm_disable=true  → success=true
      2. GET /settings               → wifi_perm_disable=true, wifi absent
      3. Reboot
      4. Wait for device to come back
      5. Re-auth
      6. GET /settings               → wifi_perm_disable=true, wifi absent (persisted in NVS)
      7. GET /info                   → wifi.enabled=false, wifi.perm_disabled=true
      8. POST /wifi_scan/start       → success=false
      9. POST wifi_perm_disable=false → success=true (QEMU: clears the flag immediately in memory)
     10. GET /settings               → wifi_perm_disable=false (in-memory already cleared;
                                        teardown fixture reboots to finalise clean state)
    """
    # Step 1: activate permanent wifi disable
    response = api.update_settings({"wifi_perm_disable": True})
    assert response.status_code == 200, (
        f"POST /settings wifi_perm_disable=true failed: {response.status_code}"
    )
    assert response.json().get("success") is True, (
        f"Expected success=true, got: {response.json()}"
    )
    print("✓ POST wifi_perm_disable=true returned success=true")

    # Step 2: verify immediate effect in settings (before reboot)
    response = api.get_settings()
    assert response.status_code == 200, f"GET /settings failed: {response.status_code}"
    data = response.json()
    assert data.get("wifi_perm_disable") is True, (
        f"wifi_perm_disable should be true immediately after POST, got: {data.get('wifi_perm_disable')}"
    )
    assert "wifi" not in data, (
        "wifi group must be absent from GET /settings when wifi_perm_disable=true"
    )
    print("✓ GET /settings: wifi_perm_disable=true, wifi group absent (pre-reboot)")

    # Step 3: reboot to apply the NVS change
    try:
        response = api.execute_command("reboot")
        print(f"  Reboot command status: {response.status_code}")
        assert response.status_code == 200, (
            f"POST /cmd reboot expected 200, got {response.status_code}"
        )
    except ConnectionError:
        print("  Connection dropped (expected during reboot)")

    # Step 4: wait for device to come back online (includes reconnect + re-auth)
    print("  Waiting for device to reboot...")
    try:
        api.wait_for_ready(timeout=1800)
    except TimeoutError:
        pytest.fail("Device did not come back within 1800 seconds after reboot")
    print("✓ Device came back online")

    # Step 5: re-auth is already done inside wait_for_ready; no extra call needed.

    # Step 6: verify wifi_perm_disable persisted across reboot
    response = api.get_settings()
    assert response.status_code == 200, f"GET /settings failed: {response.status_code}"
    data = response.json()
    assert data.get("wifi_perm_disable") is True, (
        f"wifi_perm_disable must still be true after reboot, got: {data.get('wifi_perm_disable')}"
    )
    assert "wifi" not in data, (
        "wifi group must be absent from GET /settings after reboot with wifi_perm_disable=true"
    )
    print("✓ GET /settings after reboot: wifi_perm_disable=true, wifi group absent")

    # Step 7: GET /info — wifi.enabled=false, wifi.perm_disabled=true
    response = api.get_info()
    assert response.status_code == 200, f"GET /info failed: {response.status_code}"
    info = response.json()
    assert "wifi" in info, f"Expected 'wifi' key in GET /info response, got keys: {list(info.keys())}"
    wifi_info = info["wifi"]
    assert wifi_info.get("enabled") is False, (
        f"wifi.enabled must be false when perm_disabled, got: {wifi_info.get('enabled')}"
    )
    assert wifi_info.get("perm_disabled") is True, (
        f"wifi.perm_disabled must be true in /info, got: {wifi_info.get('perm_disabled')}"
    )
    print("✓ GET /info: wifi.enabled=false, wifi.perm_disabled=true")

    # Step 8: POST /wifi_scan/start must return success=false
    response = api.start_wifi_scan()
    assert response.status_code == 200, (
        f"POST /wifi_scan/start expected 200, got {response.status_code}"
    )
    assert response.json().get("success") is False, (
        f"wifi_scan/start should return success=false when perm_disabled, got: {response.json()}"
    )
    print("✓ POST /wifi_scan/start returned success=false (wifi disabled)")

    # Step 9: POST wifi_perm_disable=false — accepted (QEMU build allows clearing the flag)
    response = api.update_settings({"wifi_perm_disable": False})
    assert response.status_code == 200, (
        f"POST wifi_perm_disable=false failed: {response.status_code}"
    )
    assert response.json().get("success") is True, (
        f"Expected success=true for false POST, got: {response.json()}"
    )
    print("✓ POST wifi_perm_disable=false returned success=true")

    # Step 10: after posting false the in-memory flag is immediately cleared (QEMU-only path);
    #           the device still needs a reboot for the wifi group to reappear.
    #           The teardown fixture handles the reboot.
    response = api.get_settings()
    assert response.status_code == 200, f"GET /settings failed: {response.status_code}"
    data = response.json()
    assert data.get("wifi_perm_disable", False) is False, (
        f"wifi_perm_disable must be false in-memory after posting false (QEMU), got: {data.get('wifi_perm_disable')}"
    )
    print("✓ GET /settings: wifi_perm_disable cleared in-memory (reboot pending in teardown)")
