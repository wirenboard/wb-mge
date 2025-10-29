#!/usr/bin/env python3

import sys
import os
import random
import argparse

# Don't change!
sec_code_size = 12
hmac_size = 32
dummy_data_arrays_count = 5

# Dummy data arrays size range
dummy_data_arrays_min_size = 15
dummy_data_arrays_max_size = 63

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


def generate_swap_tables(filename):
    with open(filename, "w") as f:
        f.write(f"# HMAC-SHA-256 key's swap table ({hmac_size} unique bytes, values range 00..{hmac_size - 1:02X})\n")
        hmac_swap = generate_unique_bytes(0, hmac_size - 1, hmac_size)
        hex_values = " ".join(f"{b:02X}" for b in hmac_swap)
        f.write(f"{hex_values}\n\n")

        f.write(f"# Security code swap table's swap table ({sec_code_size} unique bytes, values range 00..{sec_code_size - 1:02X})\n")
        swap_table_swap = generate_unique_bytes(0, sec_code_size - 1, sec_code_size)
        hex_values = " ".join(f"{b:02X}" for b in swap_table_swap)
        f.write(f"{hex_values}\n")

        print(f"Swap tables were wtitten to the '{filename}' file")


def generate_dummy_data(filename):
    with open(filename, "w") as f:
        for i in range(1, dummy_data_arrays_count + 1):
            f.write(f"# Dummy array #{i} size (decimal)\n")
            arr_size = random.randint(dummy_data_arrays_min_size, dummy_data_arrays_max_size)
            f.write(f"{arr_size}\n")

            f.write(f"# Dummy array #{i} data (hex)\n")
            arr = generate_unique_bytes(0x00, 0xFF, arr_size)
            hex_values = " ".join(f"{b:02X}" for b in arr)
            f.write(f"{hex_values}\n")

            if i < (dummy_data_arrays_count):
                f.write("\n")

        print(f"Dummy data were wtitten to the '{filename}' file")


parser = argparse.ArgumentParser(
    description=f"""\
This script can generate with random data:
1) Swap tables for headers file generation
2) Dummy data for key and swap table storage structure

Usage: {script_name} [--swap_tables OUT_SWAP_TABLES] [--dummy_data OUT_DUMMY_DATA]

Options:
  --help                            Show this help message and exit
  --swap_tables OUT_SWAP_TABLES     Output swap tables file (text format)'
  --dummy_data OUT_DUMMY_DATA       Output dummy data file (text format)'
""",
formatter_class=argparse.RawDescriptionHelpFormatter,
add_help=False
)

parser.add_argument('--help', action='store_true', help=argparse.SUPPRESS)
parser.add_argument('--swap_tables', default="", help=argparse.SUPPRESS)
parser.add_argument('--dummy_data', default="", help=argparse.SUPPRESS)

args = parser.parse_args()

if args.help:
    parser.print_help()
    sys.exit(0)

no_args = True

if len(args.swap_tables):
    generate_swap_tables(args.swap_tables)
    no_args = False

if len(args.dummy_data):
    generate_dummy_data(args.dummy_data)
    no_args = False

if no_args:
    parser.print_help()
    sys.exit(0)
