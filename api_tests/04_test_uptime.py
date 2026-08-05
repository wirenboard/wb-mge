"""Device uptime test"""



def test_uptime(api):
    """Device uptime test"""
    response = api.get_uptime()
    assert response.status_code == 200
    data = response.json()

    required_fields = ["days", "hours", "minutes", "seconds"]
    for field in required_fields:
        assert field in data, f"Field {field} is missing in uptime"

    assert isinstance(data["days"], int) and data["days"] >= 0
    assert isinstance(data["hours"], int) and 0 <= data["hours"] <= 23
    assert isinstance(data["minutes"], int) and 0 <= data["minutes"] <= 59
    assert isinstance(data["seconds"], int) and 0 <= data["seconds"] <= 59

    print("✓ Uptime retrieval works")
