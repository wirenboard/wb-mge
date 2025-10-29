#!/usr/bin/env python3

import argparse
import sys
import os
import hmac
import hashlib

# Default values
keys_file = "keys.txt"
swap_tables_file = "swap_tables.txt"
dummy_data_file = "dummy_data.txt"
out_file = "sec_code.bin"
out_header_file = "keys.h"

# Don't change!
sec_code_size = 12
hmac_size = 32

# This script name
script_name = os.path.basename(sys.argv[0])


def is_unique_and_in_range(byte_array, min, max):
    # Check that all bytes are in range
    in_range = all(min <= b <= max for b in byte_array)
    # Check that all bytes are unique
    unique = len(set(byte_array)) == len(byte_array)
    return in_range and unique


def read_mac_addr(arg):
    mac_str = arg
    for sep in ['-', '.']:
        mac_str = mac_str.replace(sep, ':')

    parts = mac_str.split(':')
    if len(parts) != 6 or not all(len(p) == 2 and all(c in '0123456789abcdefABCDEF' for c in p) for p in parts):
        print("Incorrect MAC address", file=sys.stderr)
        sys.exit(1)

    mac_bytes = bytes(int(p, 16) for p in parts)
    return mac_bytes


def read_2_hex_arrays_from_file(filename):
    if not os.path.isfile(filename):
        print(f"File '{filename}' not found", file=sys.stderr)
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
        print(f"Keys file '{filename}' must contain 2 strings with hex values", file=sys.stderr)
        sys.exit(1)

    return arrays


def read_hmac_swap_table_from_file(filename):
    arrays = read_2_hex_arrays_from_file(filename)

    hmac_key = arrays[0]
    if len(hmac_key) != hmac_size:
        print(f"HMAC key must be {hmac_size} bytes in length", file=sys.stderr)
        sys.exit(1)

    swap_table = arrays[1]
    if len(swap_table) != sec_code_size:
        print(f"Swap table must be {sec_code_size} bytes in length", file=sys.stderr)
        sys.exit(1)
    max_val = hmac_size - 1
    if not is_unique_and_in_range(swap_table, 0x00, max_val):
        print(f"Swap table must contain only unique bytes in range 0x00...{max_val:#02X}", file=sys.stderr)
        sys.exit(1)

    return hmac_key, swap_table


def read_swap_tables_from_file(filename):
    arrays = read_2_hex_arrays_from_file(filename)

    hmac_table = arrays[0]
    if len(hmac_table) != hmac_size:
        print(f"HMAC swap table must be {hmac_size} bytes in length", file=sys.stderr)
        sys.exit(1)
    max_val = hmac_size - 1
    if not is_unique_and_in_range(hmac_table, 0x00, max_val):
        print(f"HMAC swap table must contain only unique bytes in range 0x00...{max_val:#02X}", file=sys.stderr)
        sys.exit(1)

    swap_table = arrays[1]
    if len(swap_table) != sec_code_size:
        print(f"Swap table must be {sec_code_size} bytes in length", file=sys.stderr)
        sys.exit(1)
    max_val = sec_code_size - 1
    if not is_unique_and_in_range(swap_table, 0x00, max_val):
        print(f"Swap table contain only unique bytes in range 0x00...{max_val:#02X}", file=sys.stderr)
        sys.exit(1)

    return hmac_table, swap_table


def read_dummy_data_from_file(filename):
    if not os.path.isfile(filename):
        print(f"File '{filename}' not found", file=sys.stderr)
        sys.exit(1)

    with open(filename, 'r') as f:
        lines = [line.strip() for line in f if line.strip() and not line.strip().startswith('#')]

    dummy_data_arrays = []
    i = 0
    while i < len(lines):
        size = int(lines[i])
        i += 1
        hex_data = lines[i].split()
        i += 1
        if len(hex_data) != size:
            print(f"Dummy array #{i / 2 + 1} must be {size} bytes in length", file=sys.stderr)
            sys.exit(1)
        byte_array = [int(b, 16) for b in hex_data]
        dummy_data_arrays.append(byte_array)

    return dummy_data_arrays


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
        print (f"Incorrect '{filename}' file size (must be {sec_code_size} bytes)", file=sys.stderr)
        os.remove(filename)
        sys.exit(1)

    print(f"The device security code was wtitten to the '{filename}' file")


def apply_swap(values, swap_table):
    result = bytearray(len(values))
    for i, pos in enumerate(swap_table):
        result[i] = values[pos]
    return bytes(result)


def generate_keys_header_file(filename, key, key_table, swap, swap_table, dummy_data_arrays):
    with open(filename, "w") as f:
        f.write("#pragma once\n\n")
        f.write(f"// This file was generated by the {script_name} script\n\n")

        hex_values = ", ".join(f"0x{b:02X}" for b in key)
        define_str = "#define HMAC_KEY"
        spaces = " " * (32 - len(define_str))
        f.write(f"{define_str}{spaces}{{{hex_values}}}\n")

        hex_values = ", ".join(f"0x{b:02X}" for b in key_table)
        define_str = "#define HMAC_KEY_TABLE"
        spaces = " " * (32 - len(define_str))
        f.write(f"{define_str}{spaces}{{{hex_values}}}\n\n")

        hex_values = ", ".join(f"0x{b:02X}" for b in swap)
        define_str = "#define PROT_CODE_SWAP"
        spaces = " " * (32 - len(define_str))
        f.write(f"{define_str}{spaces}{{{hex_values}}}\n")

        hex_values = ", ".join(f"0x{b:02X}" for b in swap_table)
        define_str = "#define PROT_CODE_SWAP_TABLE"
        spaces = " " * (32 - len(define_str))
        f.write(f"{define_str}{spaces}{{{hex_values}}}\n\n")

        for idx, arr in enumerate(dummy_data_arrays, 1):
            define_str = f"#define DUMMY_DATA_LEN_{idx}"
            spaces = " " * (32 - len(define_str))
            f.write(f"{define_str}{spaces}{len(arr)}\n")

            hex_values = ", ".join(f"0x{b:02X}" for b in arr)
            define_str = f"#define DUMMY_DATA_ARRAY_{idx}"
            spaces = " " * (32 - len(define_str))
            f.write(f"{define_str}{spaces}{{{hex_values}}}\n")

            if (idx < len(dummy_data_arrays)):
                f.write("\n")

    print(f"The keys were wtitten to the '{filename}' file")


parser = argparse.ArgumentParser(
    description=f"""\
This script can work in one of two modes:

1) Generate security code for the specified MAC address using the keys from the specified file
Usage: {script_name} --mac MAC_ADDR [--keys KEYS_FILE] [--out OUT_FILE]

2) Generate header file for project building using the keys and the swap tables from the specified files
Usage: {script_name} [--keys KEYS_FILE] [--swap_tables SWAP_TABLES_FILE] [--dummy_data DUMMY_DATA_FILE] [--out_header OUT_HEADER_FILE]

Options:
  --help                            Show this help message and exit
  --mac MAC_ADDR                    MAC address (e.g. 01:2A:3B:4C:5D:6E)
  --keys KEYS_FILE                  Keys file (text format), default: '{keys_file}'
  --swap_tables SWAP_TABLES_FILE    Swap tables file (text format), default: '{swap_tables_file}'
  --dummy_data DUMMY_DATA_FILE      Dummy data file (text format), deefault: '{dummy_data_file}'
  --out OUT_FILE                    Output binary file with security code, default: '{out_file}'
  --out_header OUT_HEADER_FILE      Output header file for project building, default: '{out_header_file}'
""",
formatter_class=argparse.RawDescriptionHelpFormatter,
add_help=False
)

parser.add_argument('--help', action='store_true', help=argparse.SUPPRESS)
parser.add_argument('--mac', default="", help=argparse.SUPPRESS)
parser.add_argument('--keys', default=keys_file, help=argparse.SUPPRESS)
parser.add_argument('--swap_tables', default=swap_tables_file, help=argparse.SUPPRESS)
parser.add_argument('--dummy_data', default=dummy_data_file, help=argparse.SUPPRESS)
parser.add_argument('--out', default=out_file, help=argparse.SUPPRESS)
parser.add_argument('--out_header', default=out_header_file, help=argparse.SUPPRESS)

args = parser.parse_args()

if args.help:
    parser.print_help()
    sys.exit(0)

if len(args.mac):
    # Security code generation mode
    mac_addr = read_mac_addr(args.mac)
    hmac_key, swap_table = read_hmac_swap_table_from_file(args.keys)

    hmac_digest = gen_hmac(mac_addr, hmac_key)
    sec_code = gen_sec_code(hmac_digest, swap_table)

    write_sec_code_to_file(args.out, sec_code)

else:
    # Keys header file generation mode
    hmac_key, swap = read_hmac_swap_table_from_file(args.keys)
    hmac_table, swap_table = read_swap_tables_from_file(args.swap_tables)
    dummy_data_arr = read_dummy_data_from_file(args.dummy_data)

    result_hmac_key = apply_swap(hmac_key, hmac_table)
    result_swap = apply_swap(swap, swap_table)

    generate_keys_header_file(args.out_header, result_hmac_key, hmac_table, result_swap, swap_table, dummy_data_arr)
