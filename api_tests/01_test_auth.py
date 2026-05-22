"""Authentication, session management, and password change tests"""

import requests

from api_client import WBMGEAPI


def test_unauthorized_access(api):
    """Unauthorized access test"""
    unauth_session = requests.Session()

    protected_endpoints = [
        ("/info", "GET"), ("/settings", "GET"), ("/wifi_scan/start", "POST"),
        ("/wifi_scan/results", "GET"), ("/ap_clients", "GET"), ("/uptime", "GET"),
        ("/session", "GET"), ("/update", "POST")
    ]

    for endpoint, method in protected_endpoints:
        if method == "GET":
            response = unauth_session.get(f"{api.base_url}{endpoint}")
        elif method == "POST":
            response = unauth_session.post(f"{api.base_url}{endpoint}")

        print(f"Testing {method} {endpoint}:")
        print(f"  Status Code: {response.status_code}")
        print(f"  Headers: {dict(response.headers)}")
        print(f"  Content: {response.text[:200]}...")

        assert response.status_code == 401, f"Endpoint {method} {endpoint} should require authorization. Got status: {response.status_code}, content: {response.text[:100]}"

    print("✓ Protected endpoints require authorization")

    static_endpoints = ["/", "/index.css", "/index.js", "/favicon.webp"]

    for endpoint in static_endpoints:
        response = unauth_session.get(f"{api.base_url}{endpoint}")
        print(f"Testing GET {endpoint}:")
        print(f"  Status Code: {response.status_code}")
        assert response.status_code == 200

    print("✓ Static files accessible without authorization")

    hostname_response = unauth_session.get(f"{api.base_url}/hostname", timeout=10)
    assert hostname_response.status_code == 200, \
        f"GET /hostname should be accessible without auth, got {hostname_response.status_code}"
    print("✓ Hostname endpoint accessible without authorization")

    cache_protected = ["/cache/csv", "/cache/json", "/cache/status"]
    for endpoint in cache_protected:
        response = unauth_session.get(f"{api.base_url}{endpoint}", timeout=10)
        assert response.status_code == 401, \
            f"Cache endpoint {endpoint} should require auth, got {response.status_code}"
    print("✓ Cache endpoints require authorization")


def test_auth(api):
    """Authorization test"""
    response = api.auth("wrong", "wrong")
    assert response.status_code == 200
    data = response.json()
    assert data["auth"] == False
    assert "error" in data
    print("✓ Incorrect authorization rejected")

    response = api.auth()
    assert response.status_code == 200
    data = response.json()
    assert data["auth"] == True
    print("✓ Correct authorization accepted")


def test_cookie_security_attributes(api):
    """Cookie must have HttpOnly, SameSite=Strict and Path=/ attributes"""
    fresh_session = requests.Session()
    response = fresh_session.post(f"{api.base_url}/auth", json={
        "login": WBMGEAPI.DEFAULT_LOGIN, "pass": WBMGEAPI.DEFAULT_PASSWORD
    }, timeout=10)

    assert response.status_code == 200
    assert response.json()["auth"] == True

    set_cookie = response.headers.get("Set-Cookie", "")
    cookie_attrs = [attr.strip() for attr in set_cookie.split(";")]
    assert any(a.startswith("session_id=") for a in cookie_attrs), \
        "session_id missing from Set-Cookie"
    assert "HttpOnly" in cookie_attrs, "HttpOnly flag missing"
    assert "SameSite=Strict" in cookie_attrs, "SameSite=Strict missing"
    assert "Path=/" in cookie_attrs, "Path=/ missing"
    assert "Secure" not in cookie_attrs, \
        "Secure must not be set (HTTP-only device)"
    print("✓ Cookie has correct security attributes")

    failed_response = fresh_session.post(f"{api.base_url}/auth", json={
        "login": "wrong", "pass": "wrong"
    }, timeout=10)
    failed_set_cookie = failed_response.headers.get("Set-Cookie", "")
    assert "session_id=" not in failed_set_cookie, \
        "Set-Cookie must not be sent on failed login"
    print("✓ No cookie set on failed login")


def test_session_management(api):
    """Session management test"""
    response = api.get_session()
    assert response.status_code == 200
    print("✓ Session status check works")

    response = api.logout()
    assert response.status_code == 200
    data = response.json()
    assert data["logout"] == True
    print("✓ Logout works")

    response = api.get_session()
    assert response.status_code == 401
    print("✓ Session correctly terminates after logout")

    response = api.auth()
    assert response.status_code == 200
    assert response.json()["auth"] == True
    print("✓ Re-authorization works")


def test_password_change_flow(api):
    """Test POST /settings pass change + re-authentication security flow"""
    response = api.get_settings()
    assert response.status_code == 200
    settings = response.json()
    original_login = settings.get("login", "admin")
    original_password = settings.get("pass", "admin")

    new_password = "NewTestPass123"
    if new_password == original_password:
        new_password = "AnotherPass456"

    try:
        response = api.update_settings({"pass": new_password})
        assert response.status_code == 200
        assert response.json().get("success") == True
        print(f"✓ Password change accepted (new pass: {new_password})")

        response = api.get_settings()
        assert response.status_code == 200
        stored = response.json().get("pass", "")
        assert stored == new_password, \
            f"New password not stored: expected '{new_password}', got '{stored}'"
        print("✓ New password reflected in GET /settings")

        response = api.logout()
        assert response.status_code == 200
        assert response.json().get("logout") == True, "Logout must return {logout: true}"
        print("✓ Logged out")

        response = api.auth(original_login, original_password)
        assert response.status_code == 200
        assert response.json()["auth"] == False, \
            "Old password was still accepted after password change — security regression!"
        print("✓ Old password correctly rejected after password change")

        response = api.auth(original_login, new_password)
        assert response.status_code == 200
        assert response.json()["auth"] == True, \
            f"New password not accepted: {response.json()}"
        print("✓ New password accepted for re-authentication")

    finally:
        try:
            response = api.update_settings({"pass": original_password})
            if response.status_code != 200:
                api.auth(original_login, new_password)
                api.update_settings({"pass": original_password})
            print(f"✓ Original password restored")
        except Exception as exc:
            raise AssertionError(f"Failed to restore password: {exc}")
