#!/usr/bin/env python3
"""
Fast Modbus Support Probe Test Client
Usage: python3 test_fast_modbus_probe.py <IP> <PORT>
"""

import socket
import struct
import sys

def test_fast_modbus_probe(ip, port):
    # Create Modbus TCP request for Fast Modbus probe
    data_payload = b'WB-FAST-MODBUS?'
    request = struct.pack('>HHHBB', 0x0123, 0x0000, 17, 0, 0x47) + data_payload

    print(f"Connecting to {ip}:{port}...")

    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(5.0)  # 5 second timeout
            sock.connect((ip, port))
            print("Connected successfully!")

            print(f"\n=== Sending Modbus request ===")
            print(f"Request: {request.hex().upper()}")

            sock.send(request)
            print(f"Sent {len(request)} bytes")

            print(f"\n=== Waiting for response ===")
            response = sock.recv(256)
            print(f"Received {len(response)} bytes")
            print(f"Response: {response.hex().upper()}")

            if len(response) >= 8:
                # Parse Modbus TCP header
                tid, pid, length, unit_id, function = struct.unpack('>HHHBB', response[:8])

                print(f"\n=== Response Analysis ===")
                print(f"Transaction ID: 0x{tid:04X} (sent: 0x1234)")
                print(f"Protocol ID: 0x{pid:04X} (expected: 0x0000)")
                print(f"Length: {length} (expected: 19)")
                print(f"Unit ID: {unit_id} (expected: 0)")
                print(f"Function: 0x{function:02X} (expected: 0x47)")

                if function == 0x47 and unit_id == 0:
                    print(f"\n✅ Valid Fast Modbus probe response!")

                    if len(response) > 8:
                        data = response[8:].decode('ascii', errors='ignore')
                        print(f"Data payload ({len(response)-8} bytes): \"{data}\"")

                        if data == "WB-FAST-MODBUS-OK":
                            print(f"🎉 SUCCESS: Device supports WB Fast Modbus!")
                        else:
                            print(f"⚠️  Received: \"{data}\" (expected: \"WB-FAST-MODBUS-OK\")")
                    else:
                        print(f"⚠️  WARNING: No data payload in response")

                elif function & 0x80:  # Error response
                    print(f"\n❌ Modbus error response")
                    if len(response) > 8:
                        error_code = response[8]
                        print(f"Error code: 0x{error_code:02X}")
                else:
                    print(f"\n❌ Invalid or unexpected response")
            else:
                print(f"ERROR: Response too short")

    except socket.timeout:
        print("ERROR: Connection timeout")
        return False
    except ConnectionRefusedError:
        print("ERROR: Connection refused")
        return False
    except Exception as e:
        print(f"ERROR: {e}")
        return False

    return True

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 test_fast_modbus_probe.py <IP_ADDRESS> <PORT>")
        print("Example: python3 test_fast_modbus_probe.py 192.168.1.100 502")
        sys.exit(1)

    ip = sys.argv[1]
    port = int(sys.argv[2])

    success = test_fast_modbus_probe(ip, port)
    sys.exit(0 if success else 1)
