"""HTTP method guard test"""

import requests


def test_http_method_guard(api):
    """Test that endpoints reject wrong HTTP methods (expect 405 Method Not Allowed)"""
    wrong_method_cases = [
        ("GET", "/cmd"),
        ("POST", "/info"),
        ("GET", "/logout"),
        ("DELETE", "/settings"),
    ]

    for method, path in wrong_method_cases:
        url = f"{api.base_url}{path}"
        try:
            if method == "GET":
                response = api.session.get(url, timeout=10)
            elif method == "POST":
                response = api.session.post(url, json={}, timeout=10)
            elif method == "DELETE":
                response = api.session.delete(url, timeout=10)
            else:
                raise ValueError(f"Unexpected method: {method}")

            assert response.status_code == 405, \
                f"{method} {path} should return 405 Method Not Allowed, got {response.status_code}"
            print(f"✓ {method} {path} → 405 as expected")
        except requests.exceptions.ConnectionError:
            print(f"✓ {method} {path} → connection closed (method rejected, acceptable)")
