#!/usr/bin/env python3

import sys
import os
import argparse
import requests
import json

# Default values
default_ip = "192.168.0.7"
default_port = 80
default_login = "admin"
default_pass = "admin"

timeout = 10

# This script name
script_name = os.path.basename(sys.argv[0])


def authorize(ip, port, login, passwd):
    url = f"http://{ip}:{port}/auth"
    data = {
        'login': login,
        'pass': passwd
    }

    try:
        response = requests.post(url, json=data, timeout=timeout)
        if response.status_code != 200:
            print(f"Authorization response with error code: {response.status_code}", file=sys.stderr)
            sys.exit(1)
        if not response.text:
            print("Authorization response is empty", file=sys.stderr)
            sys.exit(1)

        resp_data = json.loads(response.text)
        value = resp_data.get('auth')
        if value is None:
            print("Field 'auth' is not found in the authorization response", file=sys.stderr)
            sys.exit(1)
        if value is not True:
            print("Authorization rejected", file=sys.stderr)
            sys.exit(1)

        session_id = response.cookies.get('session_id')
        if session_id is None:
            print("Session ID is not found in the authorization response", file=sys.stderr)
            sys.exit(1)
        return session_id

    except requests.exceptions.Timeout:
        print("Connection timeout", file=sys.stderr)
        sys.exit(1)

    except requests.exceptions.RequestException as e:
        print(f"Exception occurred: {e}", file=sys.stderr)
        sys.exit(1)


def get_status(ip, port, session_id):
    url = f"http://{ip}:{port}/wb_test"
    cookies = {'session_id': session_id}

    try:
        response = requests.get(url, cookies=cookies, timeout=timeout)
        if response.status_code != 200:
            print(f"Status response with error code: {response.status_code}", file=sys.stderr)
            sys.exit(1)
        if not response.text:
            print("Status response is empty", file=sys.stderr)
            sys.exit(1)

        resp_data = json.loads(response.text)
        value = resp_data.get('clock_out')
        if value is None:
            print("Field 'clock_out' is not found in the status response", file=sys.stderr)
            sys.exit(1)
        if not isinstance(value, bool):
            print("Field 'clock_out' is not boolean", file=sys.stderr)
            sys.exit(1)

        print(f"Clock output: {'enabled' if value else 'disabled'}")

    except requests.exceptions.Timeout:
        print("Connection timeout", file=sys.stderr)
        sys.exit(1)

    except requests.exceptions.RequestException as e:
        print(f"Exception occurred: {e}", file=sys.stderr)
        sys.exit(1)


def set_clock_out(ip, port, session_id, enabled):
    url = f"http://{ip}:{port}/wb_test"
    cookies = {'session_id': session_id}
    data = {'clock_out': enabled}

    try:
        response = requests.post(url, cookies=cookies, json=data, timeout=timeout)
        if response.status_code != 200:
            print(f"Clock output setup response with error code: {response.status_code}", file=sys.stderr)
            sys.exit(1)
        if not response.text:
            print("Clock output setup response is empty", file=sys.stderr)
            sys.exit(1)

        resp_data = json.loads(response.text)
        success = resp_data.get('success')
        if (success is None) or (not isinstance(success, bool)) or (not success):
            print("Clock output setup failed", file=sys.stderr)
            sys.exit(1)

        value = resp_data.get('clock_out')
        if value is None:
            print("Field 'clock_out' is not found in the clock output setup response", file=sys.stderr)
            sys.exit(1)
        if not isinstance(value, bool):
            print("Field 'clock_out' is not boolean", file=sys.stderr)
            sys.exit(1)

        print(f"Clock output: {'enabled' if value else 'disabled'}")

    except requests.exceptions.Timeout:
        print("Connection timeout", file=sys.stderr)
        sys.exit(1)

    except requests.exceptions.RequestException as e:
        print(f"Exception occurred: {e}", file=sys.stderr)
        sys.exit(1)


parser = argparse.ArgumentParser(
    description=f"""\
This script can be used for:

1) Send WB Test commands
Usage: {script_name} [--ip IP_ADDRESS] [--p PORT] [--login LOGIN] [--passwd PASSWORD] --clock_out 1|0

2) Get WB Test status
Usage: {script_name} [--ip IP_ADDRESS] [--p PORT] [--login LOGIN] [--passwd PASSWORD] --status

Options:
  --help                Show this help message and exit
  --ip IP_ADDRESS       Device IP address, default: {default_ip}
  --p PORT              Device port, default: {default_port}
  --login LOGIN         Login, default: {default_login}
  --passwd PASSWORD     Password, default: {default_pass}
  --clock_out 1|0       Enable (1) or disable (0) clock output
  --status              Show status
""",
formatter_class=argparse.RawDescriptionHelpFormatter,
add_help=False
)

parser.add_argument('--help', action='store_true', help=argparse.SUPPRESS)
parser.add_argument('--ip', default=default_ip, help=argparse.SUPPRESS)
parser.add_argument('--p', default=default_port, help=argparse.SUPPRESS)
parser.add_argument('--login', default=default_login, help=argparse.SUPPRESS)
parser.add_argument('--passwd', default=default_pass, help=argparse.SUPPRESS)
parser.add_argument('--clock_out', choices=['0', '1'], help=argparse.SUPPRESS)
parser.add_argument('--status', action='store_true', help=argparse.SUPPRESS)

args = parser.parse_args()

if args.help:
    parser.print_help()
    sys.exit(0)

if args.status:
    print("Status request")
    session_id = authorize(args.ip, args.p, args.login, args.passwd)
    get_status(args.ip, args.p, session_id)
    sys.exit(0)

if args.clock_out is not None:
    print("Clock output control")
    session_id = authorize(args.ip, args.p, args.login, args.passwd)
    clock_out = True if args.clock_out == '1' else False
    set_clock_out(args.ip, args.p, session_id, clock_out)
    sys.exit(0)

parser.print_help()
