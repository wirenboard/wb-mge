"""Heap-leak guard for the long no-reboot working session.

The suite runs against a single QEMU boot (the `qemu_process`/`api` fixtures are
session-scoped). conftest.pytest_collection_modifyitems pushes every device-reboot
test to the very end, so the body of the suite executes as one continuous session
without a heap reset. This module brackets that session:

  * test_heap_baseline  — forced first (marker `heap_baseline`); records free heap
    right after boot, while the device is quiescent.
  * test_heap_no_leak   — forced last of the no-reboot body (marker `heap_final`,
    placed just before the deferred reboot tests); records free heap again and
    asserts it did not drop by more than HEAP_LEAK_TOLERANCE_BYTES versus baseline.

Free heap fluctuates with in-flight allocations, so each measurement takes the max
free heap over a few samples (the most quiescent reading) to avoid flagging a
transient buffer as a leak. heap_min_free is reported for diagnostics only.
"""

import time

import pytest


# Allowed shrink of free internal heap across the whole no-reboot session. This
# absorbs legitimate, non-leaking retention (lazy caches, allocator fragmentation,
# the bounded auth-session ring buffer) — not a per-test budget. Tuned empirically
# against the measured baseline->final delta with margin; see PR notes.
# Allowed shrink of free internal heap from the start to the end of the no-reboot
# session. Measured baseline->final delta across many full runs is a consistent
# ~+21 KB (free heap is HIGHER at the end: boot/init scratch is reclaimed early), with
# well under 1 KB run-to-run variance. So free heap reliably ends ABOVE where it began;
# this tolerance only fires if it instead ends >16 KB BELOW baseline — a real, gross
# leak — leaving a ~37 KB margin against observed values (no flake risk). Kept as an
# absolute "not significantly below baseline" check so it stays portable across hosts
# (the natural reclaim amount is QEMU/flash-speed dependent and must not be assumed).
HEAP_LEAK_TOLERANCE_BYTES = 16 * 1024

_SAMPLES = 4
_SAMPLE_GAP_S = 0.25


def _free_heap_quiescent(api):
    """Return the max free internal heap over a few samples (most-quiescent reading),
    plus the last full /info payload for diagnostics."""
    best = -1
    data = None
    for _ in range(_SAMPLES):
        resp = api.get_info()
        assert resp.status_code == 200, f"/info returned {resp.status_code}"
        data = resp.json()
        free = int(data["heap_free"])
        if free > best:
            best = free
        time.sleep(_SAMPLE_GAP_S)
    return best, data


@pytest.mark.qemu
@pytest.mark.heap_baseline
def test_heap_baseline(api, request):
    """Record baseline free heap at the start of the continuous session."""
    free, data = _free_heap_quiescent(api)
    request.config._heap_baseline = free
    print(
        f"\n[heap] baseline free={free} B  "
        f"(total={data['heap_total']} B, min_free_since_boot={data['heap_min_free']} B)"
    )
    assert free > 0


@pytest.mark.qemu
@pytest.mark.heap_final
def test_heap_no_leak(api, request):
    """Assert free heap did not leak over the whole no-reboot session."""
    baseline = getattr(request.config, "_heap_baseline", None)
    assert baseline is not None, (
        "heap baseline was not recorded — pytest_collection_modifyitems ordering is "
        "broken or test_heap_baseline did not run"
    )
    free, data = _free_heap_quiescent(api)
    delta = baseline - free  # positive => heap shrank over the session
    print(
        f"\n[heap] final free={free} B  baseline={baseline} B  "
        f"delta={delta} B (tolerance {HEAP_LEAK_TOLERANCE_BYTES} B)  "
        f"min_free_since_boot={data['heap_min_free']} B"
    )
    assert delta <= HEAP_LEAK_TOLERANCE_BYTES, (
        f"Possible heap leak: free internal heap dropped by {delta} B over the "
        f"no-reboot session (baseline {baseline} -> final {free}), exceeding the "
        f"{HEAP_LEAK_TOLERANCE_BYTES} B tolerance. Inspect tests that open "
        f"sockets/WebSockets/sniffer/cache servers for missing cleanup."
    )
