"""Command execution tests"""

import pytest


@pytest.mark.order(18)
def test_cmd_extended(api):
    """Test POST /cmd — set_default_settings and invalid values"""
    save_response = api.get_settings()
    assert save_response.status_code == 200, "Failed to read settings before set_default_settings"
    saved_settings = save_response.json()

    try:
        print("Sending set_default_settings command...")
        response = api.execute_command("set_default_settings")
        assert response.status_code == 200, \
            f"POST /cmd set_default_settings expected 200, got {response.status_code}"
        print("✓ Command set_default_settings accepted")

        after_response = api.get_settings()
        assert after_response.status_code == 200, "Failed to read settings after set_default_settings"
        after_settings = after_response.json()
        print(f"  Settings after reset retrieved (keys: {list(after_settings.keys())})")
        print("✓ Settings readable after set_default_settings")

        response = api.session.post(f"{api.base_url}/cmd", json={"cmd": "shutdown"}, timeout=10)
        assert response.status_code == 200, \
            f"POST /cmd 'shutdown' expected 200, got {response.status_code}"
        data = response.json()
        assert data.get("success") == False, \
            f"Server must not accept unknown command 'shutdown' as successful: {data}"
        print("✓ Unknown command 'shutdown' rejected with success=false")

        response = api.session.post(f"{api.base_url}/cmd", json={}, timeout=10)
        assert response.status_code == 400, \
            f"POST /cmd empty body expected 400, got {response.status_code}"
        print("✓ Missing cmd field rejected with 400")

        response = api.session.post(f"{api.base_url}/cmd", json={"cmd": 42}, timeout=10)
        assert response.status_code == 400, \
            f"POST /cmd integer cmd expected 400, got {response.status_code}"
        print("✓ Integer cmd field rejected with 400")

    finally:
        restore_response = api.update_settings(saved_settings)
        assert restore_response.status_code == 200, \
            f"Settings restore returned status {restore_response.status_code}"
        print("✓ Settings restored after set_default_settings")
