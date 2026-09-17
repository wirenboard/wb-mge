"""Heap-leak guard for the long no-reboot working session.

The suite runs against a single QEMU boot (the `qemu_process`/`api` fixtures are
session-scoped). conftest.pytest_collection_modifyitems pushes every device-reboot
test to the very end, so the body of the suite executes as one continuous session
without a heap reset. This module brackets that session:

  * test_heap_baseline  — forced first (marker `heap_baseline`); records free heap
    once boot is OVER and the device is quiescent. "Once boot is over" is not the same
    as "once the HTTP server answers", which is all conftest's readiness probe proves —
    see _wait_boot_settled for what is still to be started at that moment and for the
    false leak (measured at 19.5-20.8 KB) that sampling too early produced in CI.
  * test_heap_no_leak   — forced last of the no-reboot body (marker `heap_final`,
    placed just before the deferred reboot tests); records free heap again and
    asserts it did not drop by more than HEAP_LEAK_TOLERANCE_BYTES versus baseline.

Free heap fluctuates with in-flight allocations, so each measurement takes the max
free heap over a few samples (the most quiescent reading) to avoid flagging a
transient buffer as a leak. heap_min_free is reported for diagnostics only.
"""

import time

import pytest


# Allowed shrink of free internal heap from the start to the end of the no-reboot
# session. Kept as an absolute "not significantly below baseline" check so it stays
# portable across hosts. It absorbs legitimate, non-leaking retention (lazy caches,
# allocator fragmentation, the bounded auth-session ring buffer); it is not a per-test
# budget.
#
# THE ~37 KB OF MARGIN THIS COMMENT USED TO CLAIM IS NOT THERE, and nothing may be sized
# on it again. It read "the measured baseline->final delta across many full runs is a
# consistent ~+21 KB (free heap is HIGHER at the end: boot/init scratch is reclaimed
# early), with well under 1 KB run-to-run variance". A full 229-item local run on this
# tree measures delta = +1772 B: free heap ends 1.8 KB BELOW baseline, not 21 KB above
# it. Whatever produced the old figure, it does not reproduce here, so the real distance
# to this tolerance is ~14.6 KB, not ~37 KB. Do not re-derive a tolerance from a remembered
# delta — measure it, and note which host it was measured on.
#
# The variance that DOES exist is in the baseline, not in the final: the same commit
# measured final=267783 locally and final=267775 on the CI node (8 B apart) while their
# baselines differed by 19 000 B. _wait_boot_settled is what closes that.
#
# EVERY ABSOLUTE FIGURE ABOVE PREDATES THE DIY STRIP and was measured against the gateway
# firmware and its 229-item suite. The shape of the argument survives (measure the delta,
# do not remember it); the numbers do not. Re-measure before quoting any of them.
HEAP_LEAK_TOLERANCE_BYTES = 16 * 1024

_SAMPLES = 4
_SAMPLE_GAP_S = 0.25

# WHY THE BASELINE WAITS FOR THE DEVICE TO FINISH BOOTING, AND IS NOT SIMPLY TAKEN WHEN THE
# HTTP SERVER ANSWERS.
#
# conftest declares the device ready as soon as GET /favicon.webp succeeds
# (_wait_for_qemu_ready in conftest.py) — i.e. as soon as http_server_init() has returned.
# Boot is NOT over at that point: virtual_io_init(), indication_init() and
# config_button_init() come after it, and so does the RS-485 port bring-up, which main.c
# reaches only after a loop that samples the link state ONCE PER SECOND. Between them they
# start two UART drivers (rx/tx ring buffers, event queue, per-port RX buffers) and several
# 3-4 KB task stacks.
#
# A baseline sampled in that window is not a baseline. It reads a device that has not finished
# booting, and every byte still to be allocated reappears at the end of the session as a
# "leak" — and `max` over the samples makes it worse, not better, because the earliest and
# highest pre-init sample is exactly the one it keeps.
#
# MEASURED, by delaying the port bring-up by 8 s so the race is lost deterministically:
# baseline 288755 B -> final 269259 B, delta 19496 B, over the 16 KB tolerance, on a session
# that ran NO TESTS AT ALL between the two samples. CI build #21 reported baseline 288555 ->
# final 267775, delta 20780, with the whole suite in between — the same baseline error,
# reported as a leak.
#
# THE CRITERION IS HEAP STABILITY ALONE, AND THAT IS A DELIBERATE WEAKENING.
#
# This guard used to carry a POSITIVE signal as well: it compared /info's rs485_N.port_mode
# against /settings' and watched /info's cache_modbus_active_port, either of which PROVED the
# port manager had entered its body. Both fields belonged to the gateway, and the DIY firmware
# no longer exposes either — /info's rs485_N objects carry no field that changes when the
# ports come up. Nothing observable over REST replaced them, so the wait now certifies a
# baseline on the heap-only criterion that the positive signal was originally added to
# reinforce.
#
# What that costs: heap stability is necessary but not sufficient. A device that has not
# started its port bring-up at all is perfectly steady, so a long enough stall before the
# bring-up — main.c's once-per-second link-state loop still waiting for the interface — now
# reads as "settled" where the port_mode comparison would have caught it. The window is
# _BOOT_SETTLE_STABLE_S wide, so the stall has to cover the whole of it. If a run ends in a
# leak of about the size of one port's bring-up, suspect that first.
#
# What it does NOT cost: the CI build #21 failure this guard was written for is still caught.
# There the heap was falling THROUGH the sample, which _HEAP_UNSETTLED reports directly.
_BOOT_SETTLE_MAX_S = 30.0
_BOOT_SETTLE_STABLE_S = 3.0
_BOOT_SETTLE_POLL_S = 0.25
# A drop smaller than this is a request being served, not a subsystem starting up. Sized well
# under the smallest thing on the boot path (a 3072-byte task stack) and well over the churn of
# one /info round trip.
_BOOT_SETTLE_NOISE_B = 1024

# The verdicts. _WAIT_ERRORED is the only one _wait_boot_settled never returns —
# test_heap_baseline publishes it up front so a raising wait still leaves a diagnosis.
_SETTLED = "settled"
_HEAP_UNSETTLED = "heap-unsettled"
_WAIT_ERRORED = "wait-errored"
# The verdicts on which a baseline may be sampled and compared against at the end of the
# session. Anything else fails in test_heap_baseline and skips test_heap_no_leak.
_USABLE_VERDICTS = (_SETTLED,)


def _wait_boot_settled(api):
    """Wait until free heap has stopped falling. Returns (verdict, samples, free).

    `free` is the last sample and is what the failure reports are written against. See the
    note over _BOOT_SETTLE_MAX_S for what this criterion can and cannot prove.
    """
    start = time.monotonic()
    deadline = start + _BOOT_SETTLE_MAX_S
    prev = None
    heap_steady_since = start
    samples = 0
    free = 0
    while True:
        resp = api.get_info()
        assert resp.status_code == 200, f"/info returned {resp.status_code}"
        info = resp.json()
        free = int(info["heap_free"])
        samples += 1
        now = time.monotonic()

        if prev is not None and free < prev - _BOOT_SETTLE_NOISE_B:
            heap_steady_since = now                 # something is still allocating
        if now - heap_steady_since >= _BOOT_SETTLE_STABLE_S:
            return _SETTLED, samples, free
        prev = free

        if now >= deadline:
            return _HEAP_UNSETTLED, samples, free
        time.sleep(_BOOT_SETTLE_POLL_S)



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
# A WAIT, not a budget, and pytest.ini's 180 s is a budget. pytest-timeout charges setup +
# call + teardown to the item, and this one is the session's first, so all three are unusually
# large here:
#   setup    — the QEMU flash/efuse build, which qemu_process runs itself unless
#              --qemu-skip-build is passed (the "--- Build ---" block in conftest.py's
#              qemu_process) and which pulls in the full IDF build through
#              qemu-create-flash-image: build-idf-project-qemu (qemu.mk:148);
#              then QEMU startup (up to conftest's QEMU_READY_TIMEOUT = 900 s) and the
#              once-per-session GET /settings that _rs485_session_baseline takes (~15.9 s
#              measured on the CI node);
#   call     — another GET /settings and up to _BOOT_SETTLE_MAX_S of polling;
#   teardown — _restore_rs485_settings, whose module-scoped teardown fires inside the LAST item
#              of each module entry and so lands here, this file's first entry holding only
#              test_heap_baseline: 41.2 s ceiling (the "Resulting ceilings" block on
#              conftest.py's _RS485_HTTP_TIMEOUT).
@pytest.mark.timeout(1200)
def test_heap_baseline(api, request):
    """Record baseline free heap at the start of the continuous session.

    Waits for the post-HTTP boot allocations to land first — see _wait_boot_settled and the
    note above it. Without that the baseline can be ~20 KB too high and the session ends with
    a "leak" of exactly that size.

    A baseline that CANNOT be trusted fails HERE rather than being carried forward. The
    verdict is published either way, and test_heap_no_leak skips itself on the bad one: the
    fact is detected at this point and so must be reported at this point, while the leak
    assert has nothing left to compare against and must not manufacture a second, derivative
    failure out of a number it already knows is wrong.

    "EITHER WAY" INCLUDES RAISING, which is why the verdict is published UP FRONT and only
    overwritten once a baseline actually exists. Publishing it after the wait covered returned
    verdicts only, and every other exit from here is an exception: the status_code asserts,
    KeyError('heap_free'), or a ReadTimeout out of api_client
    (10 s per /info, and this wait issues up to ~120 of them during the most fragile phase of
    boot). With none published, test_heap_no_leak saw `verdict is None`, skipped its guard and
    failed with "pytest_collection_modifyitems ordering is broken" — sending the reader after a
    sorting hook that has nothing to do with it, which is the same wrong-direction recurrence
    the note over _BOOT_SETTLE_MAX_S documents. _WAIT_ERRORED is not in _USABLE_VERDICTS, so it
    degrades to that skip, and the traceback of the real exception stays the diagnosis. The
    same applies to the sampling below, not only to the wait — hence "once a baseline actually
    exists" rather than "once the wait returns".
    """
    request.config._heap_baseline_verdict = _WAIT_ERRORED
    verdict, settle_samples, settle_free = _wait_boot_settled(api)

    if verdict not in _USABLE_VERDICTS:
        # Name the real cause for test_heap_no_leak's skip. Only the UNUSABLE verdicts are
        # published here, deliberately: this test cannot get past the asserts below on one of
        # them, so there is no path on which a baseline is still to be recorded. Publishing a
        # usable verdict at this point would reopen the very hole above — _free_heap_quiescent
        # can raise too, and test_heap_no_leak would then read a usable verdict with no
        # baseline behind it and fall through to the "ordering is broken" assert.
        request.config._heap_baseline_verdict = verdict

    why = {
        _HEAP_UNSETTLED: (
            f"free internal heap was STILL FALLING (last sample {settle_free} B; a drop over "
            f"{_BOOT_SETTLE_NOISE_B} B landed inside the final {_BOOT_SETTLE_STABLE_S:.0f} s). "
            f"Something on this device is allocating right now, so a baseline sampled at this "
            f"moment WOULD read high by whatever that is, and the session would end with a "
            f"false 'Possible heap leak' of exactly that size — which is what CI build #21 was"
        ),
    }.get(verdict)
    assert why is None, (
        f"NO USABLE HEAP BASELINE, and this is NOT a leak. The {_BOOT_SETTLE_MAX_S:.0f} s "
        f"boot-settled wait gave up after {settle_samples} samples without ever seeing boot "
        f"finish: {why}. The run stops here because the baseline cannot be TRUSTED. "
        f"test_heap_no_leak skips itself rather than reporting a leak this run cannot "
        f"evidence; fix the boot, or raise _BOOT_SETTLE_MAX_S if this node is genuinely this "
        f"slow."
    )

    free, data = _free_heap_quiescent(api)
    request.config._heap_baseline = free
    # Published only NOW, with the baseline it qualifies already recorded. Anything that raises
    # above this line leaves the up-front _WAIT_ERRORED standing, so test_heap_no_leak skips
    # with an accurate reason instead of blaming the collection order — see the docstring.
    request.config._heap_baseline_verdict = verdict
    note = f"boot allocations settled after {settle_samples} samples"
    print(
        f"\n[heap] baseline free={free} B  "
        f"(total={data['heap_total']} B, min_free_since_boot={data['heap_min_free']} B, "
        f"{note})"
    )
    assert free > 0


@pytest.mark.qemu
@pytest.mark.heap_final
def test_heap_no_leak(api, request):
    """Assert free heap did not leak over the whole no-reboot session."""
    verdict = getattr(request.config, "_heap_baseline_verdict", None)
    if verdict is not None and verdict not in _USABLE_VERDICTS:
        # test_heap_baseline has already failed, loudly and with the diagnosis. Comparing
        # against a baseline it declared unusable could only produce a second red item
        # accusing a leak that the run has no evidence for — which is the exact failure mode
        # this whole wait exists to remove.
        pytest.skip(
            f"no usable heap baseline (verdict '{verdict}'), so test_heap_baseline failed and "
            f"there is nothing sound to compare against. That failure is the diagnosis; a leak "
            f"verdict computed here would not be one. ('{_WAIT_ERRORED}' means it did not even "
            f"get that far — it raised before recording a baseline, and its own traceback is "
            f"the diagnosis.)"
        )
    baseline = getattr(request.config, "_heap_baseline", None)
    assert baseline is not None, (
        "heap baseline was not recorded while the verdict says it should have been — "
        "pytest_collection_modifyitems ordering is broken, or test_heap_baseline never ran "
        "at all (its fixtures failed, so not even the up-front verdict was published)"
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
        f"{HEAP_LEAK_TOLERANCE_BYTES} B tolerance. Inspect tests that open sockets or "
        f"WebSockets for missing cleanup. "
        f"CONTEXT ON THE BASELINE, so it is not suspected for the wrong reason: "
        f"_wait_boot_settled reported '{verdict}' when it was taken, and only '{_SETTLED}' "
        f"reaches this assert — free heap had been steady for {_BOOT_SETTLE_STABLE_S:.0f} s "
        f"when the sample was taken. That criterion is necessary but NOT sufficient (see the "
        f"note over _BOOT_SETTLE_MAX_S): a device whose port bring-up had not started yet is "
        f"equally steady, and its remaining allocations would land AFTER the sample, leaving "
        f"the baseline HIGH by that much. That reads here as a leak of about one port's "
        f"bring-up, so check it first if the delta is of that order. The baseline that WOULD "
        f"explain a big delta — sampled while boot allocations were still running, ~20 KB "
        f"high, CI build #21 — is reported as '{_HEAP_UNSETTLED}', which fails in "
        f"test_heap_baseline and skips this test rather than reaching this line."
    )
