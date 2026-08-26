"""The "one e2e run per WORKING TREE" lock — shared by qemu.mk and conftest.py.

WHAT IT PROTECTS. A port slot (see qemu_ports.py) makes two runs' HOST PORTS disjoint and
says nothing about the FILES they share, which are per-TREE and not per-slot:
build/qemu_flash.bin (QEMU writes NVS back into it, and `make qemu-create-flash-image`
truncates and rewrites it), build/qemu_efuse.bin, build/qemu_test.log (opened "w", i.e.
truncated) and build/qemu_test_report.xml. Two runs in ONE tree corrupt each other whatever
their slots are, most quietly through the log — which silently breaks the reboot-marker scan
in 33_test_auth_settings.py and the crash-marker scan in 16_test_uart_teardown_crash.py.

WHERE THE LOCK FILE LIVES, and why not under build/. flock is held on an INODE. Any file
that a build step deletes or replaces stops being a barrier the moment it is replaced: the
holder keeps a lock on an orphaned inode while a second process creates a NEW file at the
same path and locks that one, and both runs proceed believing they own the tree. build/ is
exactly such a place — `build-idf-project-qemu` runs `idf.py fullclean` whenever it finds a
hardware CMakeCache, and fullclean removes the whole build directory including dot-files, so
a lock at build/.e2e-tree.lock is destroyed by the very build the lock is meant to bracket
(and that is the NORMAL path in CI, where the hardware build runs before the e2e stage).
The lock therefore lives in the REPO ROOT, which nothing in the build pipeline removes
(`make qemu-clean` removes build/ and the generated sdkconfigs, not the checkout).

Not /tmp/wb-mge-e2e-<hash>.lock either, although nothing deletes that during a run:
  * the resource being protected is the TREE, so the lock belongs beside it — anyone who
    can see the tree can see the lock, with no hashing convention to agree on;
  * /tmp is namespaced. Two containers that bind-mount the SAME workspace (a normal CI
    shape) get two different /tmp, so the two runs would not see each other's lock at all —
    silently reintroducing the corruption this exists to stop. A lock inside the shared
    tree cannot have that failure mode;
  * /tmp is swept by tmp reapers on both Linux and macOS, which is the same
    "someone else deletes my inode" class of bug as build/, just rarer.
The one thing the root placement costs is an untracked file in the checkout; it is listed
in .gitignore — and a gitignored file in the root is exactly what `git clean -xfd` deletes,
so "nothing removes it" is a habit, not a guarantee. TreeLock.verify_intact() is what keeps
that from silently disarming the barrier, and it runs on BOTH paths: main() below calls it
when the wrapped command exits, and conftest calls it in the qemu_process fixture. Both are
after the fact — they turn a silent hole into a loud report, they do not abort mid-flight.

PYTHON'S fcntl.flock RATHER THAN flock(1). The Makefile needs the same lock as conftest,
and flock(1) is a util-linux tool that is NOT installed on macOS, where developers run
`make qemu-test` and `make qemu-web`. Shelling out to a Python one-liner instead keeps ONE
implementation, ONE lock path and ONE refusal message on every platform the suite runs on.

NON-BLOCKING on purpose (LOCK_NB, no timeout loop). The alternative is queueing behind a
~35-minute suite, which no caller wants and CI would read as a hang; a loud immediate
refusal naming the other run is the useful answer.

Caveat, deliberately accepted: flock is ADVISORY and its semantics over network filesystems
are weaker (on NFS the Linux kernel emulates it via POSIX locks; some exotic filesystems
ignore it). Working trees here are local — a Jenkins workspace or a git worktree on local
disk — and no cheaper mechanism is stronger.

Usage as a command (this is what qemu.mk calls):
    python3 api_tests/tree_lock.py -- <command> [args...]
Acquires the lock, runs the command with it HELD for the command's whole lifetime, and
returns its exit code. The child is told about the inherited lock through $WB_MGE_E2E_TREE_LOCK
so that a nested `pytest --qemu` does not refuse itself; see inherited_lock_holder().

"The command's whole lifetime" includes the seconds after Ctrl-C, which is where the naive
version leaked the lock: the terminal signals the whole process group, so pytest and QEMU are
mid-teardown — writing the JUnit report, dumping build/qemu_test.log — exactly when the
wrapper would otherwise have returned. run_child_holding_lock() below keeps waiting instead,
with a three-strikes escape hatch.
"""
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

try:
    import fcntl                      # POSIX only
except ImportError:                   # pragma: no cover - no Windows support anyway
    fcntl = None

# Resolved on purpose, unlike conftest.PROJECT_ROOT: two invocations that reach this tree
# through different symlinked paths must end up on the SAME lock file, or the barrier is
# not one. Both spellings name the same directory on every setup we actually run.
PROJECT_ROOT = Path(__file__).resolve().parent.parent
LOCK_PATH = PROJECT_ROOT / ".e2e-tree.lock"

# Set for the child of the `tree_lock.py -- cmd` wrapper: "<holder pid> <lock path>".
# Its only purpose is to stop a nested run from refusing a lock its own ANCESTOR holds.
INHERIT_ENV = "WB_MGE_E2E_TREE_LOCK"

UNENFORCED_WARNING = (
    "fcntl is unavailable on this platform, so the one-run-per-working-tree lock is NOT "
    "enforced. Two --qemu runs in this tree would silently corrupt each other's build/ "
    "artifacts; do not start a second one."
)


class TreeLockBusy(RuntimeError):
    """Raised when another live run already owns this tree. `.holder` names it."""

    def __init__(self, holder, strerror):
        super().__init__(f"{strerror}: {holder}")
        self.holder = holder
        self.strerror = strerror


def _slot_description():
    """'<slot> (from <source>)' for the holder line, or '?' if the slot cannot be resolved.

    Guarded: qemu_ports raises on a malformed WB_MGE_PORT_SLOT, and the lock wrapper must
    not turn that into an unrelated-looking crash — the run underneath will report it.
    No sys.path juggling needed: as a script, sys.path[0] IS api_tests/; as a module, the
    only importer is conftest.py, which already imports qemu_ports from the same directory.
    """
    try:
        import qemu_ports
        return f"{qemu_ports.SLOT} (from {qemu_ports.SLOT_SOURCE})"
    except Exception:
        return "?"


def read_holder():
    """First line of the lock file — the identity the current holder wrote, or a note."""
    try:
        with open(LOCK_PATH) as fh:
            return fh.readline().strip() or "(holder has not written its identity yet)"
    except OSError as exc:
        return f"(could not read the lock file: {exc!r})"


def busy_lines(exc, this_run=None):
    """The refusal text, shared by the CLI and conftest so both say the same thing."""
    lines = [
        f"Another e2e run already owns this working tree ({exc.strerror}).",
        f"  Tree      : {PROJECT_ROOT}",
        f"  Lock file : {LOCK_PATH}",
        f"  Held by   : {exc.holder}",
    ]
    if this_run:
        lines.append(f"  This run  : {this_run}")
    lines += [
        "  A distinct WB_MGE_PORT_SLOT does NOT make this safe: the slot only",
        "  separates host PORTS, while build/qemu_flash.bin, build/qemu_efuse.bin,",
        "  build/qemu_test.log and build/qemu_test_report.xml are per-TREE. Run the",
        "  second instance from its own checkout or `git worktree`, with its own slot.",
    ]
    return lines


def lock_is_held_by_someone():
    """True when SOME open file description in the system holds the flock on LOCK_PATH.

    The one check that is complete on its own, and the one the marker checks below cannot
    make: pids get recycled and file CONTENTS go stale (nothing rewrites the file when a
    holder is SIGKILLed), but the flock is released by the kernel the moment the last
    descriptor holding it closes, however the holder died.

    A separate open() is a separate open file description, and flock conflicts between
    descriptions rather than between processes — so this is refused even when the holder is
    our own parent, which is exactly what makes it usable to verify an INHERITED lock.

    Opened read-only so the probe never creates the file it is asking about (flock does not
    care about the access mode). "No lock file" therefore answers False, which is the truth:
    nothing can be holding a lock on a file that is not there.

    Accepted cost: when nobody holds the lock the probe HOLDS IT for the microseconds
    between flock() and close(), so a genuinely simultaneous acquisition could be refused by
    a probe rather than by a run. There is no portable way to ask "is this locked" without
    locking it (/proc/locks is Linux-only), the window is two syscalls wide, and the failure
    mode is one loud refusal — not the silent sharing this module exists to prevent.
    """
    if fcntl is None:
        return True                    # cannot probe; do not invent a failure
    try:
        probe = open(LOCK_PATH)
    except OSError:
        return False
    try:
        fcntl.flock(probe.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except OSError:
        return True                    # somebody holds it — a marker naming a holder is plausible
    else:
        fcntl.flock(probe.fileno(), fcntl.LOCK_UN)
        return False                   # nobody holds it — a marker naming a holder is a lie
    finally:
        probe.close()


def inherited_lock_holder():
    """Holder line when an ANCESTOR of this process already holds this tree's lock.

    Returns None when there is no such ancestor, i.e. when the caller must take the lock
    itself. Every part of the marker is verified rather than trusted, because a stale
    exported variable in a developer's shell would otherwise disable the barrier outright:
      * the path in the marker must be THIS tree's lock file (a marker inherited from a
        run in another checkout says nothing about ours);
      * the pid must still be alive;
      * the lock FILE must still name that pid as its holder — which is what actually ties
        the marker to the lock rather than to a process that once held it;
      * and the flock must STILL BE HELD by someone. The three checks above are all
        forgeable by accident: a killed holder leaves the file naming its pid (nothing
        rewrites it), and pids are recycled, so "alive + named in the file" can be true of
        a process that never held anything. This one asks the kernel.
    A marker that fails any of these is ignored, and the caller then attempts a normal
    acquisition, which either succeeds or refuses loudly. Failing "open" here is safe in a
    way that failing "held" would not be.

    Cheap enough to re-run: it is called at acquisition AND from verify_intact() below, so
    an inherited lock is re-verified during the run instead of being trusted for ~35 minutes
    on the strength of one env var read at startup.
    """
    raw = os.environ.get(INHERIT_ENV, "").strip()
    if not raw:
        return None
    pid_str, _, path_str = raw.partition(" ")
    if path_str.strip() != str(LOCK_PATH):
        return None
    try:
        pid = int(pid_str)
    except ValueError:
        return None
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return None                    # holder is gone; the marker is stale
    except PermissionError:
        pass                           # alive, just not ours to signal
    except OSError:
        return None
    holder = read_holder()
    if not holder.startswith(f"pid={pid} "):
        return None
    if not lock_is_held_by_someone():
        return None                    # file still names it, kernel says nobody holds it
    return holder


class TreeLock:
    """An acquired (or deliberately unenforced) tree lock. Release is idempotent."""

    def __init__(self, fh, enforced=True, inherited_holder=None):
        self._fh = fh
        self.enforced = enforced
        self.inherited_holder = inherited_holder

    @property
    def inherited(self):
        return self.inherited_holder is not None

    @classmethod
    def acquire(cls, allow_inherited=True):
        """Take the lock, or raise TreeLockBusy naming the holder.

        With `allow_inherited`, a lock already held by an ancestor process (the
        `tree_lock.py -- cmd` wrapper) satisfies the caller instead of colliding with it:
        flock is per open file description, so a child that opened the file itself would be
        refused by its own parent.
        """
        if allow_inherited:
            holder = inherited_lock_holder()
            if holder is not None:
                return cls(None, enforced=True, inherited_holder=holder)
        if fcntl is None:
            return cls(None, enforced=False)
        LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
        # "a+", never "w": a REFUSED attempt must not truncate the holder's identity, which
        # is the only thing that makes the refusal message useful.
        fh = open(LOCK_PATH, "a+")
        try:
            fcntl.flock(fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            holder = read_holder()
            fh.close()
            raise TreeLockBusy(holder, exc.strerror or "locked") from exc
        fh.seek(0)
        fh.truncate()
        fh.write(f"pid={os.getpid()} slot={_slot_description()} "
                 f"tree={PROJECT_ROOT} started={time.strftime('%Y-%m-%d %H:%M:%S')}\n")
        fh.flush()
        return cls(fh)

    def verify_intact(self):
        """Re-check that this lock still keeps a second run out of the tree.

        Returns an error string, or None when the barrier is still standing. There are two
        different things to verify, and the previous version passed one of them BY
        CONSTRUCTION — it looked only at `self._fh`, which an inherited lock does not have:

        * We hold the flock ourselves. The failure to catch is a lost INODE: something
          deleted or replaced the lock file while we held it, so our flock now guards an
          orphan and a second run can lock the new file and proceed. That is precisely how a
          lock under build/ was disarmed by `idf.py fullclean`; the root placement makes it
          unlikely, and `git clean -xfd` (the file is gitignored) keeps it possible.
        * We INHERITED it from an ancestor — the shape `make qemu-test` always has, since
          the wrapper holds the lock and pytest only recognises it. There is no descriptor
          of ours to stat, and answering "fine" because of that is the guard-that-does-
          nothing this module exists to delete: the ancestor can die mid-run (Ctrl-C, an
          OOM kill), the kernel frees its flock immediately, and this process would go on
          reporting itself protected for the rest of a ~35-minute session. So the whole
          marker is re-verified, kernel probe included.

        Unenforced (no fcntl) returns None: nothing was ever claimed there, and the caller
        has already warned about it.
        """
        if self.inherited_holder is not None:
            if inherited_lock_holder() is None:
                return (f"the ancestor process that took {LOCK_PATH} for this run "
                        f"[{self.inherited_holder}] does not hold it any more; nothing is "
                        f"keeping a second run out of {PROJECT_ROOT} now")
            return None
        if self._fh is None:
            return None                # unenforced platform (see UNENFORCED_WARNING)
        try:
            on_disk = os.stat(LOCK_PATH).st_ino
        except OSError as exc:
            return (f"the lock file {LOCK_PATH} disappeared while this run held it "
                    f"({exc!r}); the lock no longer keeps a second run out of this tree")
        held = os.fstat(self._fh.fileno()).st_ino
        if on_disk != held:
            return (f"the lock file {LOCK_PATH} was replaced while this run held it "
                    f"(inode {held} -> {on_disk}); this run's flock now guards an orphaned "
                    f"inode and a second run in this tree would NOT be refused")
        return None

    def release(self):
        """Drop the lock. Idempotent; safe when nothing was ever acquired."""
        fh, self._fh = self._fh, None
        if fh is None:
            return
        try:
            # Blank the identity line BEFORE closing, while the flock still makes this
            # exclusive. Closing frees the lock but leaves the file naming this pid, and a
            # reader that only looks at the file (inherited_lock_holder, read_holder in a
            # refusal message) would then be talking about a run that ended hours ago — or,
            # once the pid is recycled, about an unrelated live process. Best effort: a
            # failure here costs a misleading line, not the lock.
            fh.seek(0)
            fh.truncate()
            fh.write(f"(nobody holds this lock; last holder pid={os.getpid()} "
                     f"released={time.strftime('%Y-%m-%d %H:%M:%S')})\n")
            fh.flush()
        except OSError:
            pass
        try:
            fh.close()                 # closing the descriptor releases the flock
        except OSError:
            pass

    def child_env(self, base=None):
        """Environment for a child that must NOT try to take this lock again."""
        env = dict(os.environ if base is None else base)
        if self._fh is not None:
            env[INHERIT_ENV] = f"{os.getpid()} {LOCK_PATH}"
        return env


# How many Ctrl-C's the wrapper absorbs before it stops waiting for the child. The first
# ones are absorbed ON PURPOSE (see run_child_holding_lock); this is the escape hatch that
# keeps "absorbed" from meaning "swallowed forever".
INTERRUPTS_BEFORE_GIVING_UP = 3


def jobserver_fds():
    """GNU make's jobserver descriptors, parsed out of $MAKEFLAGS, for Popen's pass_fds.

    Popen closes every inherited descriptor above stderr (close_fds defaults to True), and
    make older than 4.4 hands its jobserver down as a PIPE PAIR named in MAKEFLAGS —
    `--jobserver-fds=R,W` (3.81, the make on macOS) or `--jobserver-auth=R,W` (4.2+). The
    run_locked recipe line mentions $(MAKE), so make treats it as recursion and passes that
    pipe to us; closing it here makes the sub-make print "jobserver unavailable: using -j1"
    and silently serialise the firmware build under `make -j8 qemu-test`.

    make >= 4.4 defaults to a NAMED pipe (`--jobserver-auth=fifo:/path`), which the child
    reopens by path — nothing to pass through, hence the `fifo:` skip.

    Descriptors are fstat'ed before being passed on: make only opens the pipe for recipe
    lines it considers recursive, and pass_fds with a closed descriptor fails the exec
    itself, which would be a much worse outcome than losing the jobserver.
    """
    fds = []
    for word in os.environ.get("MAKEFLAGS", "").split():
        for prefix in ("--jobserver-fds=", "--jobserver-auth="):
            if not word.startswith(prefix):
                continue
            spec = word[len(prefix):]
            if spec.startswith("fifo:"):
                continue
            for part in spec.split(","):
                try:
                    fd = int(part)
                    os.fstat(fd)
                except (ValueError, OSError):
                    continue
                fds.append(fd)
    return tuple(fds)


def run_child_holding_lock(args, lock):
    """Run `args` to completion with `lock` held, and return its exit code.

    Deliberately NOT subprocess.run(). On KeyboardInterrupt CPython's run() does
    `process.kill(); raise` — it SIGKILLs the DIRECT child (make) and returns at once, while
    the GRANDCHILDREN that got their own SIGINT from the terminal are still shutting down:
    pytest is SIGTERMing QEMU, writing the --junitxml report and reading build/qemu_test.log
    in pytest_sessionfinish. The caller's `finally: lock.release()` would then hand the tree
    over in exactly those seconds, and the shell prompt comes back right then — i.e. at the
    moment a developer re-runs. Those two files are the reason the release was moved out of
    qemu_process's teardown in the first place; letting an interrupt undo it would restore
    the same race by another route.

    Ctrl-C on a terminal is delivered to the whole FOREGROUND PROCESS GROUP, so make, pytest
    and QEMU each get their own SIGINT whatever the wrapper does with its copy. The wrapper
    does not need to forward anything — only to survive it and keep waiting.

    A Python handler rather than SIG_IGN, and this is load-bearing: exec() resets HANDLED
    signals to the default in the new image but PRESERVES an ignored one. Setting SIG_IGN
    before Popen would give make (and through it pytest and QEMU) an inherited "ignore
    SIGINT", so Ctrl-C would stop working for the run entirely — the opposite of the intent.

    The escape hatch is INTERRUPTS_BEFORE_GIVING_UP: the third Ctrl-C stops the wait, kills
    the direct child and returns 130, saying plainly that the lock is being dropped while
    the grandchildren may still be writing. A user who really wants out gets out in three
    keystrokes; it just cannot happen by accident, which is what the first version did.
    """
    state = {"proc": None, "interrupts": 0}

    def on_interrupt(_signum, _frame):
        state["interrupts"] += 1
        remaining = INTERRUPTS_BEFORE_GIVING_UP - state["interrupts"]
        if remaining <= 0:
            raise KeyboardInterrupt   # main thread, at the next bytecode boundary
        proc = state["proc"]
        print(f"\ntree_lock: interrupted. The run{'' if proc is None else f' (pid {proc.pid})'} "
              f"got the same Ctrl-C from the terminal and is finishing its teardown — QEMU "
              f"shutdown, the JUnit report, the log dump. Holding {LOCK_PATH} until it exits "
              f"so the next run cannot truncate those files under it. Press Ctrl-C "
              f"{remaining} more time(s) to stop waiting for it.", file=sys.stderr)

    previous = signal.signal(signal.SIGINT, on_interrupt)
    try:
        proc = subprocess.Popen(args, env=lock.child_env(), pass_fds=jobserver_fds())
        state["proc"] = proc
        try:
            # Popen.wait() reports death BY SIGNAL as a negative number, and make dies of
            # SIGINT by re-raising it after "*** [target] Interrupt" — so the raw value is
            # -2, which sys.exit() would turn into the meaningless status 254. Translate to
            # the shell's own convention, 128 + signal, i.e. the 130 the previous version
            # returned from its `except KeyboardInterrupt`.
            returncode = proc.wait()
            return 128 - returncode if returncode < 0 else returncode
        except KeyboardInterrupt:
            pass
        print(f"\ntree_lock: giving up on pid {proc.pid} after "
              f"{INTERRUPTS_BEFORE_GIVING_UP} interrupts. Killing it and releasing "
              f"{LOCK_PATH}. Anything it started (pytest, qemu-system-xtensa) may still be "
              f"running and writing into build/ — check with "
              f"`ps -Aww -o pid=,args= | grep qemu-system-xtensa` before starting a new run.",
              file=sys.stderr)
        try:
            proc.kill()
            proc.wait(timeout=5)
        except (KeyboardInterrupt, subprocess.TimeoutExpired, OSError):
            # A FOURTH Ctrl-C lands here (the handler is still armed and still raises).
            # Swallowed on purpose: we are already on the way out, and a traceback would
            # only hide the message above. The lock goes with the process either way — the
            # kernel drops the flock when the last descriptor closes.
            pass
        return 130
    finally:
        signal.signal(signal.SIGINT, previous)


def main(argv=None):
    """`tree_lock.py -- <command...>`: run the command with this tree's lock held."""
    args = list(sys.argv[1:] if argv is None else argv)
    if args and args[0] == "--":
        args = args[1:]
    if not args:
        print(f"usage: {Path(sys.argv[0]).name} -- <command> [args...]", file=sys.stderr)
        return 2
    try:
        lock = TreeLock.acquire()
    except TreeLockBusy as exc:
        this_run = f"pid={os.getpid()} cmd={' '.join(args)}"
        print("\n".join(busy_lines(exc, this_run)), file=sys.stderr)
        return 1
    if not lock.enforced:
        print(f"WARNING: {UNENFORCED_WARNING}", file=sys.stderr)
    try:
        returncode = run_child_holding_lock(args, lock)
        # Checked HERE, not only inside pytest. On `make qemu-test` — the CI and the default
        # developer path — the build runs in the -locked target's prerequisites under THIS
        # lock, and the pytest underneath gets --qemu-skip-build, so conftest's copy of this
        # check sits behind a branch that path never takes. Without this call the claim that
        # a deleted lock file is "reported loudly instead of silently disarming the barrier"
        # held for a bare `pytest --qemu` only. It is a report, not a rescue: by the time it
        # fires the run is over, so it names the damage rather than preventing it.
        problem = lock.verify_intact()
        if problem:
            print(f"ERROR: the working-tree lock did not survive this run: {problem}",
                  file=sys.stderr)
            print(f"       {PROJECT_ROOT} was therefore unprotected for part of the run; "
                  f"treat its build/ artifacts as suspect and check for a second run.",
                  file=sys.stderr)
            return returncode or 1
        return returncode
    finally:
        lock.release()


if __name__ == "__main__":
    sys.exit(main())
