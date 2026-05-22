"""Hostname endpoint test"""

import requests


def test_hostname(api):
    """Test GET /hostname endpoint"""
    response = api.get_hostname()
    assert response.status_code == 200, \
        f"GET /hostname expected 200, got {response.status_code}"

    data = response.json()
    assert "hostname" in data, "Field 'hostname' is missing from /hostname response"
    assert isinstance(data["hostname"], str), "Field 'hostname' must be a string"
    assert len(data["hostname"]) > 0, "Field 'hostname' must not be empty"

    print(f"✓ Hostname endpoint works, hostname: {data['hostname']}")

    unauth_response = requests.get(f"{api.base_url}/hostname", timeout=10)
    assert unauth_response.status_code == 200, \
        f"GET /hostname must be accessible without auth, got {unauth_response.status_code}"
    unauth_data = unauth_response.json()
    assert unauth_data.get("hostname") == data["hostname"], \
        "Unauthenticated /hostname response must match authenticated response"
    print("✓ Hostname endpoint accessible without authorization")
