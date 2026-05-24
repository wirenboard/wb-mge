#!/usr/bin/env python3
"""Apply a single ESP-IDF patch idempotently.

Reads IDF_PATH from the environment (set by EIM_ACTIVATE when called from make).
Takes a single positional argument: the patch filename (basename only, e.g.
bug01-uart-driver-delete-intr-order.patch). The patch file is resolved relative
to the directory containing this script (patches/).

Idempotency is checked via a reverse dry-run: if the patch is already applied,
the script prints [skip] and exits 0 without modifying any files.

Usage:
    python3 patches/apply_idf_patch.py bug01-uart-driver-delete-intr-order.patch
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path


def is_applied(idf_path, patch_file):
    """Return True if the patch is already applied (reverse dry-run succeeds)."""
    result = subprocess.run(
        ["patch", "--dry-run", "-R", "-p1", "-i", str(patch_file)],
        cwd=idf_path,
        capture_output=True,
    )
    return result.returncode == 0


def apply_patch(idf_path, patch_file):
    """Apply patch to idf_path. Streams all patch output to terminal; exits with code 1 on failure."""
    result = subprocess.run(
        ["patch", "-p1", "-i", str(patch_file)],
        cwd=idf_path,
        # No capture: stdout and stderr both go to the terminal so offset warnings are visible.
    )
    if result.returncode != 0:
        print("ERROR: Failed to apply patch: {}".format(patch_file.name), file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        description="Apply a single ESP-IDF patch idempotently using reverse dry-run detection."
    )
    parser.add_argument(
        "patch_filename",
        help="Patch filename (basename only, resolved relative to this script's directory).",
    )
    args = parser.parse_args()

    # Read IDF_PATH from environment — set by EIM_ACTIVATE when called from make.
    idf_path_env = os.environ.get("IDF_PATH")
    if not idf_path_env:
        print("ERROR: IDF_PATH is not set in the environment.", file=sys.stderr)
        sys.exit(1)

    idf_path = Path(idf_path_env).resolve()
    if not idf_path.is_dir():
        print("ERROR: IDF_PATH does not exist or is not a directory: {}".format(idf_path), file=sys.stderr)
        sys.exit(1)

    # Resolve patch file relative to this script's directory (patches/).
    patches_dir = Path(__file__).parent
    patch_file = patches_dir / args.patch_filename

    if not patch_file.is_file():
        print("ERROR: Patch file not found: {}".format(patch_file), file=sys.stderr)
        sys.exit(1)

    patch_name = patch_file.name

    # Check if the patch is already applied via reverse dry-run.
    if is_applied(idf_path, patch_file):
        print("[skip] {}".format(patch_name))
        sys.exit(0)

    # Apply the patch and verify it was applied correctly.
    apply_patch(idf_path, patch_file)

    if not is_applied(idf_path, patch_file):
        print("ERROR: Patch verification failed after apply: {}".format(patch_name), file=sys.stderr)
        sys.exit(1)

    print("[ok] {}".format(patch_name))


if __name__ == "__main__":
    main()
