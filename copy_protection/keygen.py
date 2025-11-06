#!/usr/bin/env python3

import sys
import os
import random
import argparse

# Default values
default_keys_file = "keys.txt"

# Don't change!
key_len = 32
swap_table_len = 12

# This script name
script_name = os.path.basename(sys.argv[0])


def generate_unique_bytes(min_val, max_val, count):
    if count > (max_val - min_val + 1):
        print("Count value is more then available unique values range", file=sys.stderr)
        sys.exit(1)

    byte_range = list(range(min_val, max_val + 1))
    random.shuffle(byte_range)
    result = byte_range[:count]

    return result


def generate_keys_file(filename):
    with open(filename, "w") as f:
        f.write(f"# HMAC-SHA-256 key ({key_len} bytes)\n")
        key = generate_unique_bytes(0x00, 0xFF, key_len)
        hex_values = " ".join(f"{b:02X}" for b in key)
        f.write(f"{hex_values}\n\n")

        f.write(f"# Protection code swap table ({swap_table_len} unique bytes, values range 00..{key_len - 1:02X})\n")
        swap_table = generate_unique_bytes(0x00, key_len - 1, swap_table_len)
        hex_values = " ".join(f"{b:02X}" for b in swap_table)
        f.write(f"{hex_values}\n")

        print(f"Keys were wtitten to the '{filename}' file")


parser = argparse.ArgumentParser(
    description=f"""\
This script generates keys file with random data
Usage: {script_name} [--keys_file OUT_KEYS_FILE]

Options:
  --help                            Show this help message and exit
  --keys_file OUT_KEYS_FILE         Output keys file, default: '{default_keys_file}'
""",
formatter_class=argparse.RawDescriptionHelpFormatter,
add_help=False
)

parser.add_argument('--help', action='store_true', help=argparse.SUPPRESS)
parser.add_argument('--keys_file', default=default_keys_file, help=argparse.SUPPRESS)

args = parser.parse_args()

if args.help:
    parser.print_help()
    sys.exit(0)

generate_keys_file(args.keys_file)
