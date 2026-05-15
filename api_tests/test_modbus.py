"""Modbus TCP parameters and validation tests"""

import pytest


@pytest.mark.order(8)
def test_modbus_tcp_parameters(api):
    """Modbus TCP specific parameters test"""
    response = api.get_settings()
    assert response.status_code == 200
    original_settings = response.json()

    try:
        modbus_settings = {
            "rs485_1": {
                "bridge": {
                    "mode": "server",
                    "port": 502,
                    "modbus": True
                }
            },
            "rs485_2": {
                "bridge": {
                    "mode": "client",
                    "port": 503,
                    "ip": "192.168.1.10",
                    "modbus": True
                }
            }
        }

        response = api.update_settings(modbus_settings)
        assert response.status_code == 200
        result = response.json()
        assert result["success"] == True
        print("✓ Modbus TCP settings saved")

        response = api.get_settings()
        assert response.status_code == 200
        new_settings = response.json()

        rs485_1 = new_settings["rs485_1"]["bridge"]
        assert rs485_1["modbus"] == True

        rs485_2 = new_settings["rs485_2"]["bridge"]
        assert rs485_2["modbus"] == True

        print("✓ Modbus TCP parameters applied correctly")

        transparent_settings = {
            "rs485_1": {
                "bridge": {
                    "modbus": False
                }
            }
        }

        response = api.update_settings(transparent_settings)
        assert response.status_code == 200
        print("✓ Transparent mode settings accepted")
    finally:
        # Restore original RS485 bridge settings to prevent state leakage between tests
        api.update_settings(original_settings)
        print("✓ Original RS485 settings restored")


@pytest.mark.order(9)
def test_modbus_validation_limits(api):
    """Validation limits test for Modbus parameters"""
    baseline_response = api.get_settings()
    assert baseline_response.status_code == 200
    baseline_settings = baseline_response.json()

    try:
        invalid_settings = {
            "rs485_1": {
                "bridge": {
                    "modbus": True,
                    "port": 0
                }
            }
        }

        response = api.update_settings(invalid_settings)
        assert response.status_code in [200, 400]
        if response.status_code == 200:
            check_response = api.get_settings()
            assert check_response.status_code == 200
            check_settings = check_response.json()
            actual_port = check_settings["rs485_1"]["bridge"]["port"]
            assert actual_port != 0, \
                f"Invalid port 0 was saved (expected rejection, got {actual_port})"
        print("✓ Invalid port 0 is handled")

        invalid_settings = {
            "rs485_2": {
                "bridge": {
                    "modbus": True,
                    "port": 70000
                }
            }
        }

        response = api.update_settings(invalid_settings)
        assert response.status_code in [200, 400]
        if response.status_code == 200:
            check_response = api.get_settings()
            assert check_response.status_code == 200
            check_settings = check_response.json()
            actual_port = check_settings["rs485_2"]["bridge"]["port"]
            assert actual_port != 70000, \
                f"Invalid port 70000 was saved (expected rejection, got {actual_port})"
        print("✓ Port limit exceeding is handled")
    finally:
        # Restore baseline settings to prevent state leakage if invalid data slips through validation
        api.update_settings(baseline_settings)
        print("✓ Baseline RS485 settings restored")
