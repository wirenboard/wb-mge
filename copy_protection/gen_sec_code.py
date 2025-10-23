#!/usr/bin/env python3

import argparse
import sys
import os
import hmac
import hashlib

# Default values
keys_file = "keys.txt"
out_file = "sec_code.bin"

# Don't change!
sec_code_size = 12


def is_unique_and_in_range(byte_array, min, max):
    # Проверка диапазона для всех байт
    in_range = all(min <= b <= max for b in byte_array)
    # Проверка уникальности — множество хранит только уникальные значения
    unique = len(set(byte_array)) == len(byte_array)
    return in_range and unique


def read_mac_addr(arg):
    mac_str = arg
    for sep in ['-', '.']:
        mac_str = mac_str.replace(sep, ':')

    parts = mac_str.split(':')
    if len(parts) != 6 or not all(len(p) == 2 and all(c in '0123456789abcdefABCDEF' for c in p) for p in parts):
        print("Incorrect MAC address")
        sys.exit(1)

    mac_bytes = bytes(int(p, 16) for p in parts)
    return mac_bytes


def read_hmac_swap_table_from_file(filename):
    if not os.path.isfile(filename):
        print(f"File '{filename}' not found")
        sys.exit(1)

    arrays = []
    with open(filename, "r") as f:
        for line in f:
            # Ignore commented lines
            if line.startswith("#"):
                continue
            # Delete new lines and spaces
            hex_str = line.strip()
            if not hex_str:
                continue
            # Convert string to bytes
            data = bytes.fromhex(hex_str)
            arrays.append(data)

    # Check lines count
    if len(arrays) != 2:
        print(f"Keys file '{filename}' must contain 2 strings with hex values")
        sys.exit(1)

    hmac_key = arrays[0]
    if len(hmac_key) != 32:
        print("HMAC key must be 32 bytes in length")
        sys.exit(1)

    swap_table = arrays[1]
    if len(swap_table) != sec_code_size:
        print(f"Swap table must be {sec_code_size} bytes in length")
        sys.exit(1)
    if not is_unique_and_in_range(swap_table, 0x00, 0x1F):
        print("Swap table contain only unique bytes in range 0x00...0x1F")
        sys.exit(1)

    return hmac_key, swap_table


def gen_hmac(mac_addr, hmac_key):
    h = hmac.new(hmac_key, mac_addr, hashlib.sha256)
    hmac_digest = h.digest()
    return hmac_digest


def gen_sec_code(hmac_digest, swap_table):
    result = bytearray(sec_code_size)
    for i, pos in enumerate(swap_table):
        result[i] = hmac_digest[pos]
    return bytes(result)


def write_sec_code_to_file(filename, sec_code):
    with open(filename, "wb") as f:
        f.write(sec_code)

    # Extra check for file size
    size = os.path.getsize(filename)
    if size != sec_code_size:
        print (f"Incorrect '{filename}' file size (must be {sec_code_size} bytes)")
        os.remove(filename)
        sys.exit(1)

    print(f"The device security code was wtitten to the '{filename}' file")


parser = argparse.ArgumentParser(description="This script generates a security code for the specified MAC address using the keys from the specified file.")
parser.add_argument('--mac', required=True, help='MAC address (e.g. 01:2A:3B:4C:5D:6E)')
parser.add_argument('--keys', default=keys_file, help=f'Keys file (text format), default: {keys_file}')
parser.add_argument('--out', default=out_file, help=f'Output binary file, default: {out_file}')
args = parser.parse_args()

mac_addr = read_mac_addr(args.mac)
hmac_key, swap_table = read_hmac_swap_table_from_file(args.keys)

hmac_digest = gen_hmac(mac_addr, hmac_key)
sec_code = gen_sec_code(hmac_digest, swap_table)

write_sec_code_to_file(args.out, sec_code)
