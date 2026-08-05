#!/usr/bin/env python3
"""
Fast Modbus Scan — manual diagnostic tool.

This script is NOT a pytest test. It is a standalone command-line utility
for manually scanning Fast Modbus devices over TCP by sending FC 0x46
scan requests and printing the raw response.

Usage:
    python3 scan_fast_modbus.py <IP_ADDRESS> <PORT>
    python3 scan_fast_modbus.py 192.168.1.100 502
"""

import socket
import struct
import sys

def scan_fast_modbus(ip, port):
    request = struct.pack('>HHHBBB', 0x0001, 0x0000, 3, 0xFD, 0x46, 0x01)

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
        print("Usage: python3 scan_fast_modbus.py <IP_ADDRESS> <PORT>")
        print("Example: python3 scan_fast_modbus.py 192.168.1.100 502")
        sys.exit(1)

    ip = sys.argv[1]
    port = int(sys.argv[2])

    success = scan_fast_modbus(ip, port)
    sys.exit(0 if success else 1)
