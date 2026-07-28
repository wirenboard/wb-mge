#!/usr/bin/env python3
"""Apply a single ESP-IDF patch idempotently.

Reads IDF_PATH from the environment (set by EIM_ACTIVATE when called from make).
Takes a single positional argument: the patch filename (basename only, e.g.
bug01-uart-driver-delete-intr-order.patch). The patch file is resolved relative
to the directory containing this script (patches/).

Idempotency is checked PER HUNK via marker strings taken from the patch body:
every hunk contributes its own marker (the first added line of that hunk that is
distinctive enough), looked up in the file that hunk belongs to. All markers
present -> the patch is applied, the script prints [skip] and exits 0 without
touching anything. None present -> the patch is applied. Some present and some
not -> the tree is half-patched and the script fails loudly; that state used to
be reported as a plain [skip], which left the remaining hunks unapplied forever.

The marker check replaced an earlier reverse dry-run (`patch --dry-run -R`).
That check was broken: on an UNPATCHED file GNU patch prints "Unreversed (or
previously applied) patch detected!  Ignore -R? [y]" and, with a non-interactive
stdin, answers itself with the default "yes" — it then applies the patch forward
in the dry run and exits 0. is_applied() therefore returned True for a pristine
tree and the patch was never applied. Every `patch` invocation here now passes
--batch (never prompt) and --forward (never apply in reverse) so patch can no
longer answer its own question, and the dry-run fallback parses the output
instead of trusting the exit code alone.

Limitations:
  * Patches that CREATE a file (`--- /dev/null`) are not supported.
    _parse_hunks() and backup_originals() only understand `--- a/<path>`
    headers, so such a hunk has no file to look its marker up in and the
    post-apply verification could never succeed. The script rejects them
    explicitly instead of guessing. None of the current patches create files.
  * A marker is a single line. Lines that the patch also deletes or keeps as
    context are rejected as indistinguishable (that is the "moved line" trap),
    but a short added line that happens to occur elsewhere in the target file
    can still masquerade as "already applied". Prefer patches whose added block
    starts with a comment naming the fix, as all four current ones do.

Usage:
    python3 patches/apply_idf_patch.py bug01-uart-driver-delete-intr-order.patch
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import namedtuple
from pathlib import Path


# One hunk of a unified diff.
#   ordinal: 1-based position in the patch file, for error messages
#   target : path of the patched file relative to IDF_PATH, None for `/dev/null`
#   added  : the hunk's '+' lines, in order, with the '+' removed
_Hunk = namedtuple("_Hunk", "ordinal target added")

_FILE_HEADER_RE = re.compile(r"^--- (\S+)")
_OLD_PATH_RE = re.compile(r"^--- a/(\S+)")
_HUNK_HEADER_RE = re.compile(r"^@@ -\d+(?:,(\d+))? \+\d+(?:,(\d+))? @@")

# Shortest string accepted as a marker, after stripping. Anything below this is
# punctuation noise ("}", "});") that says nothing about whether a fix is in.
_MIN_MARKER_LEN = 3


def _parse_hunks(patch_file):
    """Split a unified diff into hunks, and collect the lines no marker may use.

    Returns (hunks, forbidden):
      hunks     — list of _Hunk in file order.
      forbidden — stripped text of every deleted and context line in the patch.
                  A line the patch removes or merely keeps around already exists
                  in the unpatched file (or exists nowhere after the patch), so
                  it can never tell "applied" from "not applied" apart.

    Hunk bodies are consumed by the line counts in the `@@` header rather than by
    pattern-matching, so a body line that happens to look like a `--- a/...`
    header cannot be mistaken for one.
    """
    lines = [line.rstrip("\r") for line in patch_file.read_text(errors="replace").splitlines()]
    hunks = []
    forbidden = set()
    target = None
    index = 0
    while index < len(lines):
        line = lines[index]
        index += 1

        header = _FILE_HEADER_RE.match(line)
        if header:
            old_path = _OLD_PATH_RE.match(line)
            target = old_path.group(1) if old_path else None
            continue

        hunk_header = _HUNK_HEADER_RE.match(line)
        if not hunk_header:
            continue

        # "@@ -a,b +c,d @@" — an omitted count means 1.
        old_remaining = int(hunk_header.group(1)) if hunk_header.group(1) else 1
        new_remaining = int(hunk_header.group(2)) if hunk_header.group(2) else 1
        added = []
        while index < len(lines) and (old_remaining > 0 or new_remaining > 0):
            body = lines[index]
            index += 1
            if body.startswith("\\"):        # "\ No newline at end of file"
                continue
            if body.startswith("+"):
                added.append(body[1:])
                new_remaining -= 1
            elif body.startswith("-"):
                forbidden.add(body[1:].strip())
                old_remaining -= 1
            else:                            # context line (" ", or emptied by a stripper)
                forbidden.add(body[1:].strip() if body.startswith(" ") else body.strip())
                old_remaining -= 1
                new_remaining -= 1
        hunks.append(_Hunk(ordinal=len(hunks) + 1, target=target, added=added))
    return hunks, forbidden


def _hunk_marker(hunk, forbidden):
    """Pick a distinguishable idempotency marker for one hunk, or None.

    The first added line that (a) is long enough to mean something, (b) carries
    at least one alphanumeric character, and (c) is not also deleted or kept as
    context anywhere in this patch. Rule (c) is what rejects a merely REORDERED
    line: it exists in the unpatched file too, so finding it would report every
    pristine tree as already patched.
    """
    for line in hunk.added:
        text = line.strip()
        if len(text) < _MIN_MARKER_LEN:
            continue
        if not any(char.isalnum() for char in text):
            continue
        if text in forbidden:
            continue
        return text
    return None


def _describe(hunk):
    return "hunk #{} ({})".format(hunk.ordinal, hunk.target)


def _marker_present_in_target(idf_path, hunk, marker):
    """Return True if 'marker' is in the file THIS hunk patches.

    Scoping the lookup to the hunk's own file matters: bug05 and bug06 open with
    the very same added line and are told apart only by the file they live in.
    """
    target = idf_path / hunk.target
    if not target.is_file():
        return False
    return marker in target.read_text(errors="replace")


# Flags shared by every `patch` invocation in this script.
#   --batch  : never ask a question. Without it patch answers its own prompt on a
#              non-interactive stdin and does the opposite of what we asked.
#   --forward: never apply a patch in reverse; an already-applied patch is
#              reported ("Reversed (or previously applied) patch detected!")
#              instead of being silently undone.
# The reject file is added per invocation by _run_patch().
PATCH_SAFETY_FLAGS = ["--batch", "--forward"]


def _spawn_patch(idf_path, patch_file, extra_flags, capture, reject_file):
    """Run one `patch` process with the safety flags and an explicit -r file."""
    return subprocess.run(
        ["patch"] + list(extra_flags) + PATCH_SAFETY_FLAGS
        + ["-r", str(reject_file), "-p1", "-i", str(patch_file)],
        cwd=idf_path,
        capture_output=capture,
        text=capture,
    )


def _run_patch(idf_path, patch_file, extra_flags=(), capture=False, reject_file=None):
    """Run `patch` inside idf_path with the safety flags and a -r reject file.

    Rejects never land as the default <file>.rej beside the source: a
    false-negative marker makes us run `patch` on an already-patched tree, and
    Apple patch litters the IDF checkout with reject files even under --batch
    --forward. (`-r /dev/null` also works but makes Apple patch print a bogus
    "Can't backup ... Operation not permitted" line.)

    reject_file=None sends them to a directory deleted on return. That is right
    for a dry run: it changed nothing, so there is nothing left to diagnose.
    A real apply passes a path that outlives this call — see apply_patch().
    """
    if reject_file is None:
        with tempfile.TemporaryDirectory(prefix="wb-idf-patch-") as tmpdir:
            return _spawn_patch(idf_path, patch_file, extra_flags, capture, Path(tmpdir) / "reject")
    return _spawn_patch(idf_path, patch_file, extra_flags, capture, reject_file)

# Substring common to what both implementations print when they recognise an
# already-applied patch:
#   GNU      "Reversed (or previously applied) patch detected!  Skipping patch."
#   BSD/Apple "Ignoring previously applied (or reversed) patch."
_ALREADY_APPLIED_MARK = "previously applied"


def _is_applied_by_dry_run(idf_path, patch_file):
    """Fallback idempotency check: a forward dry-run whose OUTPUT is parsed.

    Only used when NO hunk of the patch yields a distinguishable marker, i.e.
    when the per-hunk check has nothing to work with at all. Exits with a
    non-zero code on any outcome that is not unambiguously "applies cleanly" or
    "already applied" — a silent [skip] on a guess is what this script exists to
    avoid.
    """
    result = _run_patch(idf_path, patch_file, extra_flags=["--dry-run"], capture=True)
    output = (result.stdout or "") + (result.stderr or "")
    lowered = output.lower()
    already_applied = _ALREADY_APPLIED_MARK in lowered
    hunk_failed = "failed" in lowered

    if result.returncode == 0 and not already_applied and not hunk_failed:
        # The patch applies cleanly to the tree as it is, so it is not applied yet.
        return False
    if already_applied and not hunk_failed:
        return True

    print(
        "ERROR: cannot determine whether {} is applied "
        "(patch --dry-run exit {}).".format(patch_file.name, result.returncode),
        file=sys.stderr,
    )
    print(output.rstrip(), file=sys.stderr)
    sys.exit(1)


def _fail(message, details=()):
    print("ERROR: {}".format(message), file=sys.stderr)
    for detail in details:
        print("       {}".format(detail), file=sys.stderr)
    sys.exit(1)


def is_applied(idf_path, patch_file):
    """Return True if EVERY hunk of the patch is already applied.

    Primary check: one distinctive added line per hunk (see _hunk_marker), looked
    up in that hunk's own file. It needs no cooperation from `patch` and stays
    correct even when the surrounding context has drifted since the patch was
    first applied.
    Fallback (no hunk yields a distinguishable marker): a forward dry-run whose
    output is parsed.
    Anything in between — a half-applied patch, or a patch where only some hunks
    have a usable marker — exits non-zero. Returning a verdict there would mean
    guessing, and a wrong guess is a silent [skip] that never applies the rest.
    """
    hunks, forbidden = _parse_hunks(patch_file)
    if not hunks:
        _fail("no hunks found in {} — is it a unified diff?".format(patch_file.name))

    unsupported = [hunk for hunk in hunks if hunk.target is None]
    if unsupported:
        _fail(
            "{} contains hunks without an `--- a/<path>` header.".format(patch_file.name),
            ["Patches that create new files (--- /dev/null) are not supported."],
        )

    markers = [(hunk, _hunk_marker(hunk, forbidden)) for hunk in hunks]
    with_marker = [(hunk, marker) for hunk, marker in markers if marker]
    without_marker = [hunk for hunk, marker in markers if not marker]

    if not with_marker:
        # Nothing in the patch is distinctive enough to search for; fall back to
        # asking `patch` itself, and abort on any answer that is not clear-cut.
        return _is_applied_by_dry_run(idf_path, patch_file)

    if without_marker:
        _fail(
            "cannot verify {}: no distinguishable marker in some hunks.".format(patch_file.name),
            ["no marker: {}".format(_describe(hunk)) for hunk in without_marker]
            + [
                "A marker must be an added line that the patch does not also",
                "delete or keep as context. Add a distinctive comment to those",
                "hunks, or split the patch.",
            ],
        )

    present = []
    missing = []
    for hunk, marker in with_marker:
        (present if _marker_present_in_target(idf_path, hunk, marker) else missing).append(hunk)

    if not present:
        return False
    if not missing:
        return True

    _fail(
        "{} is only PARTIALLY applied — the IDF tree is half-patched.".format(patch_file.name),
        ["applied    : {}".format(_describe(hunk)) for hunk in present]
        + ["not applied: {}".format(_describe(hunk)) for hunk in missing]
        + [
            "Restore the affected files (each has a <file>.orig backup made by",
            "this script) or reinstall the IDF, then re-run the patch step.",
        ],
    )


def apply_patch(idf_path, patch_file):
    """Apply patch to idf_path. Streams all patch output to terminal; exits with code 1 on failure."""
    # The rejects of a real apply are the whole diagnosis of WHICH hunk did not
    # fit and against what, so they go to a directory that outlives this process
    # — patch used to name a path inside a TemporaryDirectory that was already
    # gone by the time the error was printed. Still a temp directory rather than
    # the IDF checkout: a stale .rej left in the tree would only confuse the next
    # run. The directory is removed again unless patch actually wrote a reject.
    reject_dir = Path(tempfile.mkdtemp(prefix="wb-idf-patch-"))
    reject_file = reject_dir / "reject"
    # capture=False: stdout and stderr both go to the terminal so offset warnings are visible.
    result = _run_patch(idf_path, patch_file, reject_file=reject_file)
    if result.returncode != 0:
        details = []
        if reject_file.exists():
            details.append("rejected hunks saved to: {}".format(reject_file))
        else:
            shutil.rmtree(reject_dir, ignore_errors=True)
        _fail("Failed to apply patch: {}".format(patch_file.name), details)
    shutil.rmtree(reject_dir, ignore_errors=True)


def backup_originals(idf_path: Path, patch_file: Path) -> None:
    """Copy each file touched by patch_file to <file>.orig if the backup does not yet exist."""
    lines = patch_file.read_text(errors="replace").splitlines()
    for line in lines:
        match = re.match(r"^--- a/(\S+)", line)
        if not match:
            continue
        rel_path = match.group(1).rstrip("\r")  # strip CR if the patch file has CRLF line endings
        src = idf_path / rel_path
        orig = Path(str(src) + ".orig")
        if not src.is_file():
            print("[backup-warn] source not found, skipping: {}".format(rel_path), file=sys.stderr)
            continue
        if orig.is_file():
            print("[backup-skip] {}".format(rel_path))
        else:
            shutil.copy2(src, orig)
            print("[backup] {}".format(rel_path))


def main():
    parser = argparse.ArgumentParser(
        description="Apply a single ESP-IDF patch idempotently using marker-based detection."
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

    # Check if the patch is already applied (marker lookup, dry-run fallback).
    if is_applied(idf_path, patch_file):
        print("[skip] {}".format(patch_name))
        sys.exit(0)

    # Back up original files before applying so they can be restored if needed.
    backup_originals(idf_path, patch_file)

    # Apply the patch and verify it was applied correctly.
    apply_patch(idf_path, patch_file)

    if not is_applied(idf_path, patch_file):
        print("ERROR: Patch verification failed after apply: {}".format(patch_name), file=sys.stderr)
        sys.exit(1)

    print("[ok] {}".format(patch_name))


if __name__ == "__main__":
    main()
