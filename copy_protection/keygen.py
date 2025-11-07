#!/usr/bin/env python3

import sys
import os
import random
import argparse
import hmac
import hashlib

# Default values
default_keys_file = "keys.txt"

# Don't change!
key_len = 32
swap_table_len = 12

# This script name
script_name = os.path.basename(sys.argv[0])


def write_keys_to_file(filename, hmac_key, swap_table):
    with open(filename, "w") as f:
        f.write(f"# HMAC-SHA-256 key ({key_len} bytes)\n")
        hex_values = " ".join(f"{b:02X}" for b in hmac_key)
        f.write(f"{hex_values}\n\n")

        f.write(f"# Protection code swap table ({swap_table_len} unique bytes, values range 00..{key_len - 1:02X})\n")
        hex_values = " ".join(f"{b:02X}" for b in swap_table)
        f.write(f"{hex_values}\n")

        print(f"Keys were wtitten to the '{filename}' file")


def generate_random_unique_bytes(min_val, max_val, count):
    if count > (max_val - min_val + 1):
        print("Count value is more then available unique values range", file=sys.stderr)
        sys.exit(1)

    byte_range = list(range(min_val, max_val + 1))
    random.shuffle(byte_range)
    result = byte_range[:count]

    return result


def generate_random_keys_file(filename):
    hmac_key = generate_random_unique_bytes(0x00, 0xFF, key_len)
    swap_table = generate_random_unique_bytes(0x00, key_len - 1, swap_table_len)
    write_keys_to_file(filename, hmac_key, swap_table)


def compute_sha512_from_stream(stream):
    data = stream.read()
    h = hmac.new(data, digestmod=hashlib.sha512)
    hmac_digest = h.digest()
    return hmac_digest


def get_digest_from_file(filename):
    if not os.path.isfile(filename):
        print(f"File '{filename}' not found", file=sys.stderr)
        sys.exit(1)

    with open(filename, 'rb') as f:
        digest = compute_sha512_from_stream(f)

    return digest


def get_digest_from_stdin():
    digest = compute_sha512_from_stream(sys.stdin.buffer)
    return digest


def prepare_swap_table(in_array):
    # Limit the input values modulo key_len
    mod_values = [b % key_len for b in in_array]
    seen = set()
    result = []
    # Collect unique values
    for v in mod_values:
        if v not in seen:
            seen.add(v)
            result.append(v)
        if len(result) == swap_table_len:
            break
    # If unique values count is less than swap_table_len, add missing values from the range (key_len-1)...0
    if len(result) < swap_table_len:
        for i in range(key_len - 1, -1, -1):
            if i not in seen:
                result.append(i)
                if len(result) == swap_table_len:
                    break
    return result


def generate_keys_file(in_data_file, out_keys_file):
    if len(in_data_file):
        digest = get_digest_from_file(in_data_file)
    else:
        digest = get_digest_from_stdin()

    hmac_key = digest[:key_len]
    swap_table = prepare_swap_table(digest[key_len:])
    write_keys_to_file(out_keys_file, hmac_key, swap_table)


parser = argparse.ArgumentParser(
    description=f"""\
This script can generate keys file in 2 modes:
1) With specified input data:
Usage: {script_name} [--in_data IN_DATA_FILE] [--keys_file OUT_KEYS_FILE]
2) With random data
Usage: {script_name} --random [--keys_file OUT_KEYS_FILE]

Options:
  --help                            Show this help message and exit
  --random                          Generate keys file with random data
  --in_data IN_DATA_FILE            Input data file name, if not specified data is read from the stdin stream
  --keys_file OUT_KEYS_FILE         Output keys file, default: '{default_keys_file}'
""",
formatter_class=argparse.RawDescriptionHelpFormatter,
add_help=False
)

parser.add_argument('--help', action='store_true', help=argparse.SUPPRESS)
parser.add_argument('--random', action='store_true', help=argparse.SUPPRESS)
parser.add_argument('--in_data', default='', help=argparse.SUPPRESS)
parser.add_argument('--keys_file', default=default_keys_file, help=argparse.SUPPRESS)

args = parser.parse_args()

if args.help:
    parser.print_help()
    sys.exit(0)

if args.random:
    generate_random_keys_file(args.keys_file)
    sys.exit(0)

generate_keys_file(args.in_data, args.keys_file)
