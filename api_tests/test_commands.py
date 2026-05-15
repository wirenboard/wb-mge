"""Command execution tests"""

import pytest
import requests


@pytest.mark.order(17)
def test_commands(api):
    """Command execution test"""
    save_response = api.get_settings()
    assert save_response.status_code == 200
    saved_settings = save_response.json()

    try:
        print("Sending set_default_settings command...")
        response = api.execute_command("set_default_settings")

        print(f"Status Code: {response.status_code}")
        print(f"Headers: {response.headers}")
        print(f"Content: {response.text[:500]}...")

        assert response.status_code == 200, f"Expected status 200, got {response.status_code}"

        assert response.text.strip(), \
            f"set_default_settings command returned an empty response body"
        data = response.json()
        assert data.get("success") == True, \
            f"set_default_settings command did not return success=true: {data}"
        print(f"JSON Response: {data}")

        print("✓ Command set_default_settings works")

    except requests.exceptions.RequestException as e:
        print(f"Connection error executing command: {e}")
        raise
    except Exception as e:
        print(f"Unexpected error in commands test: {e}")
        print(f"Error type: {type(e).__name__}")
        raise
    finally:
        restore_response = api.update_settings(saved_settings)
        assert restore_response.status_code == 200, \
            f"Settings restore returned status {restore_response.status_code}"
        print("✓ Settings restored after set_default_settings")


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
        assert response.status_code in [200, 400], \
            f"POST /cmd 'shutdown' got unexpected status {response.status_code}"
        if response.status_code == 200:
            data = response.json()
            assert data.get("success") == False, \
                f"Server accepted unknown command 'shutdown' as successful: {data}"
            print("  [INFO] Server returned 200 for unknown command with success=false (lenient validation)")
        else:
            print("✓ Unknown command 'shutdown' rejected with 400")

        response = api.session.post(f"{api.base_url}/cmd", json={}, timeout=10)
        assert response.status_code in [200, 400], \
            f"POST /cmd empty body got unexpected status {response.status_code}"
        print("✓ Missing cmd field handled")

        response = api.session.post(f"{api.base_url}/cmd", json={"cmd": 42}, timeout=10)
        assert response.status_code in [200, 400], \
            f"POST /cmd integer cmd got unexpected status {response.status_code}"
        print("✓ Integer cmd field handled")

    finally:
        restore_response = api.update_settings(saved_settings)
        assert restore_response.status_code == 200, \
            f"Settings restore returned status {restore_response.status_code}"
        print("✓ Settings restored after set_default_settings")
