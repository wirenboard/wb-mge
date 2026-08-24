"""Integration tests: LED indication (indication.c) + factory-reset long-press.

Covers indication.c via the network-activity LEDs and the >5 s config-button
long-press factory reset (main.c config_button_longpress_callback ->
factory_reset() + settings_update()).

LED bit mapping on the virtual IO state bus (see leds_control.c):
    Eth LED  -> E05 (inverted in HW; we only assert it CHANGES)
    WiFi LED -> E04 (inverted in HW)
    Status   -> E07 (covered in test 44)

The factory-reset test is destructive (it wipes settings to defaults), so it is
marked @pytest.mark.reboot and is deferred to the very end of the suite by
conftest.pytest_collection_modifyitems. The other two tests are non-destructive.
"""

import os
import re
import time

import pytest
import requests

from io_bus_helpers import IoBus

pytestmark = pytest.mark.qemu


# --------------------------------------------------------------------------------------
# Factory-reset long press: timing budget
# --------------------------------------------------------------------------------------

# The firmware threshold the hold must clear: main/main.c:39
# CONFIG_BTN_FACTORY_RESET_HOLD_TIME_MS = 5000, handed to config_button at main.c:235.
# Kept here so the margin below is expressed as a RATIO against the real constant rather
# than against a number remembered in prose.
_FW_LONG_PRESS_THRESHOLD_S = 5.0

# How long the host holds G34 LOW. 2.4x the firmware threshold.
#
# WHY SO MUCH MORE THAN "5 s PLUS A BIT", i.e. why this is not padding. The firmware does
# not measure the hold from the instant the host sent the datagram; main/config_button.c:104
# stamps `long_press_time_stamp = sys_time` when the button task OBSERVES the press — after
# a 50 ms debounce (CONFIG_BUTTON_DEBOUNCE_TIME_MS) sampled every 10 ms
# (CONFIG_BUTTON_POLL_PERIOD_MS), on a guest that also has to receive the UDP datagram
# through slirp first. Everything between "the host sent G34/0" and "the button task ran"
# therefore comes straight off the top of the margin. On a contended CI node that delay is
# not milliseconds: the same investigation that produced this change measured a single
# POST /settings taking 15.88 s on that node with four QEMU e2e suites running at once. The
# previous 6.0 s hold left a 1 s / 20% margin and build #45 duly lost it — the device
# answered 200 throughout, the reset simply never triggered. The margin has to absorb the
# guest's OBSERVATION DELAY, which is the actual failure mode, so it is sized on scheduling
# jitter rather than on the threshold.
#
# AND IT COSTS NOTHING TO OBSERVE. main/config_button.c:114-120 fires the long-press
# callback as soon as `hold_time` crosses the threshold WHILE THE BUTTON IS STILL HELD, then
# clears `pending_long_press`, so the callback runs exactly once no matter how much longer
# the button stays down. A longer hold does not lengthen the event, does not repeat the
# factory reset, and does not change what the firmware logs — it only widens the window in
# which a starved guest can still notice the press in time. The extra seconds also give the
# reset a HEAD START before the revert poll begins — not a guarantee that it finishes, since
# the long press only fires once the guest has accumulated 5 s of ITS OWN clock and on a
# starved node that can land near the end of the hold, leaving the NVS writes to happen
# after release.
_FACTORY_RESET_HOLD_S = 12.0

# How long to wait for the reverted setting to become visible over HTTP.
#
# Widened from 8 s for the same reason the hold was, and it is not free-floating: the
# convicting branches of the classification below accuse the firmware of not applying a
# reset it demonstrably performed, so this deadline must be long enough that "we did not
# wait long enough" is not a live explanation. Against the 15.88 s worst POST /settings
# measured on the loaded node, 30 s is ~2x.
#
# It is also a term in this item's time budget. That budget is worked out in ONE place — the
# @pytest.mark.timeout comment on test_factory_reset_long_press — so changing this constant
# means recomputing the arithmetic there, and nowhere else.
_REVERT_POLL_MAX_S = 30.0

# Slop above the firmware threshold before the guest's own clock is allowed to convict the
# firmware of a long-press regression (case 2 below). It covers the button task's 10 ms
# sampling period, the 50 ms release debounce, and the 1 ms granularity of the log
# timestamp — none of which the lower bound computed in _guest_held_ms_after_press models.
#
# The rest of that bound is conservative in the acquitting direction, but only because two
# specific things are done deliberately, and BOTH were wrong in an earlier draft of this
# change — neither is a property the arithmetic has on its own:
#   - the serial scan stamps `read_at` AFTER its read, not before (_button_markers_since);
#   - press_button() measures its hold on time.monotonic() rather than the wall clock an NTP
#     step can move under a 12 s hold, and hands back its own release stamp
#     (io_bus_helpers.py) so the caller never reconstructs that instant against a second
#     clock the helper was not using.
# Get either wrong and the bound rises, which means it accuses the firmware. This margin is
# not what protects against that; it only absorbs the modelling gaps listed above.
_LONG_PRESS_VERDICT_MARGIN_S = 1.0


# --------------------------------------------------------------------------------------
# Factory-reset long press: firmware serial markers
# --------------------------------------------------------------------------------------
#
# The firmware logs several distinct stages of the long press, which is what makes a
# multi-way verdict possible when the setting does not revert — which of "the press never
# arrived", "the press arrived but the hold fell short", "the reset started and had not
# finished yet" and "the reset finished and did not do its job" actually happened. Every
# string below is a literal prefix of an ESP_LOG format string in the firmware, verified
# against the source at the cited line.
#
# ALL OF THEM ARE COMPILED IN for the QEMU build, per the TRACKED sdkconfig defaults —
# sdkconfig.qemu.minimal:1300 (CONFIG_LOG_DEFAULT_LEVEL=3, so INFO is emitted at runtime)
# and :1304 (CONFIG_LOG_MAXIMUM_LEVEL=4, which is what keeps ESP_LOGI in the binary at all).
# Cited against sdkconfig.qemu.minimal and NOT against sdkconfig.qemu_build: the latter is
# generated, is ignored by .gitignore:24 and is deleted by `qemu-clean` (qemu.mk:371), so a
# reader on a fresh clone would find no such file.
#
# THE ORDER THEY APPEAR IN MATTERS, and three of the five mean "about to start", not "done".
# main/main.c:57-63 runs: ESP_LOGW(":59 triggered") -> blink -> factory_reset(), which is
# ESP_LOGI(":49 resetting") -> setting_items_set_defaults(false) -> ESP_LOGI(":52
# completed"). The work in the middle (main/setting_items.c:261-283) is a loop of
# storage_iface->write_str() over all 55 entries of setting_items[], each with its own
# ESP_LOGI, running in config_button_task at priority 2 (main/config_button.c:14, created
# at :149) — below the IDF httpd task at priority 5, i.e. below the very poll that is asking
# whether the reset landed. rs485_1.term is KEY_485_TERM_1, entry 30 of those 55, so a guest
# that has printed "Resetting all settings to factory defaults..." can easily not have
# reached it yet. Only _MARK_RESET_COMPLETED proves the work is finished, and only it may
# gate a firmware accusation.

# main/config_button.c:106 — the firmware OBSERVED the press (post-debounce edge).
_MARK_PRESS = "Button press event, counter:"

# main/config_button.c:116 — the observed hold reached the threshold.
#
# DO NOT ASSERT ON THE NUMBER THIS LINE CARRIES. The event fires at the FIRST sample where
# `hold_time >= threshold` and immediately clears `pending_long_press`
# (main/config_button.c:114-120), so the logged value is always ~5000 ms whether the host
# held the button for 6 s or for 30 s. It is not a measurement of how long the button was
# held and it cannot be used to check that _FACTORY_RESET_HOLD_S took effect. The useful
# signal is WHICH markers appeared, never what they say.
_MARK_LONG_PRESS = "Button long press event, hold time:"

# main/main.c:59 (ESP_LOGW) and :49 — the callback ran and the reset is ABOUT TO START.
# Neither says the settings were rewritten; see the note above.
_MARK_RESET_TRIGGERED = "Factory reset triggered by 5-second config button hold!"
_MARK_RESET_STARTED = "Resetting all settings to factory defaults..."

# main/main.c:52 — proves setting_items_set_defaults() returned, i.e. that all 55 keys were
# written. The full line is "Factory reset completed! Settings will revert to defaults.";
# this is a prefix of it.
_MARK_RESET_COMPLETED = "Factory reset completed!"

# main/setting_items.c:274 — ESP_LOGI(TAG, "Set default %s = %s", item->key, default_value),
# emitted per key on a SUCCESSFUL storage_iface->write_str(). item->key for rs485_1.term is
# KEY_485_TERM_1 = "485_term_1" (main/setting_items.h:55), so the line reads literally
# "Set default 485_term_1 = true". (:276 logs "Failed to set default for %s" and returns the
# error instead, so this line appearing means the write itself reported ESP_OK.)
#
# WHY THIS EXISTS AT ALL: it closes the one branch that otherwise had no way to convict.
# The reset writes 55 keys one at a time and only announces completion at the end, so a
# setting_items_set_defaults() that hung or died at key N would produce "triggered, not
# completed" forever — a permanent skip blaming the node. This marker is the per-key
# equivalent of _MARK_RESET_COMPLETED for the one key this test reads back, it is of exactly
# the same evidential quality (the firmware says it wrote it and got ESP_OK), and it arrives
# earlier. Convicting on it is therefore sound even when the loop never finishes.
_MARK_TERM_KEY_WRITTEN = "Set default 485_term_1 = "

# Guest-clock timestamp at the head of every ESP-IDF log line: "I (12345) tag: message".
# Milliseconds since boot off the RTOS tick count — sdkconfig.qemu.minimal:1324
# (CONFIG_LOG_TIMESTAMP_SOURCE_RTOS=y, with :1325 confirming SOURCE_SYSTEM is not set) — and
# unprefixed by ANSI escapes because :1323 leaves CONFIG_LOG_COLORS unset.
_LOG_TS_RE = re.compile(r"^[VDIWE] \((\d+)\)")

# Bounded re-read of the serial log before drawing a verdict from the ABSENCE of a marker.
# Two jobs. QEMU writes the log from its own process, so a line can lag the event it
# describes and scanning once would let that lag masquerade as "the firmware never got
# there". And the marker the scan waits for is _MARK_RESET_COMPLETED, which trails the
# trigger by however long 55 priority-2 NVS writes take on a node that is preempting them —
# so the scan is also the last grace period the reset gets. Only paid on the failure path,
# and it exits the moment that marker appears.
_SERIAL_SCAN_MAX_S = 10.0

# Same stable, greppable marker the sibling guard on this branch uses
# (37_test_cache_server_deinit_hang.py::_skip_detector_disarmed), so runs abandoned because
# the ENVIRONMENT invalidated the measurement can be counted across builds with one grep
# whichever file produced them.
_DISARMED_MARKER = "DETECTOR-DISARMED"


def _qemu_serial_log_path():
    """Path to the live QEMU serial capture (QEMU_LOG_PATH in conftest.py:47)."""
    return os.path.join(os.path.dirname(__file__), "..", "build", "qemu_test.log")


def _serial_log_offset():
    """Current size of the QEMU serial log, or None if it cannot be read.

    Captured BEFORE the button press, so the scan below only ever sees serial emitted by
    this press. Scanning the whole file instead would be actively wrong rather than merely
    imprecise: several earlier tests in this suite press the config button, so
    "Button press event" is already in the log by the time this item runs and a press that
    never reached the guest would classify as one that did. (The same trap was fixed once
    already in 33_test_auth_settings.py, whose _serial_log_offset this mirrors.)

    An offset into a file only one run may be writing: conftest opens it with "w" at QEMU
    launch and holds an exclusive lock on the working tree for the whole session, so nothing
    truncates it under us mid-session.
    """
    try:
        return os.path.getsize(_qemu_serial_log_path())
    except OSError:
        return None


def _parse_log_timestamps(tail):
    """Return (last press-event timestamp, newest timestamp) in ms of GUEST clock, or Nones.

    Lines without a parseable header are skipped rather than guessed at, so a torn final
    line (the scan can read while QEMU is mid-write) contributes nothing instead of a wrong
    number.
    """
    press_ts = None
    last_ts = None
    for line in tail.splitlines():
        match = _LOG_TS_RE.match(line)
        if match is None:
            continue
        ts = int(match.group(1))
        last_ts = ts
        if _MARK_PRESS in line:
            press_ts = ts
    return press_ts, last_ts


def _guest_held_ms_after_press(markers, release_at_host):
    """LOWER bound on the guest's OWN elapsed ms between it observing the press and the host
    releasing the button. None when the log carries no usable timestamps.

    WHY A BOUND AND NOT A MEASUREMENT. The quantity that decides whether a missing long press
    is the firmware's fault is how much of ITS OWN clock the guest lived through while it
    knew the button was down: the firmware fires at the first sample where
    `sys_time - long_press_time_stamp` reaches the threshold, and `pending_long_press` is
    cleared only once the button task itself has debounced a release
    (main/config_button.c:104-121). So if the guest's clock advanced past the threshold
    between the press it logged and the moment the host stopped asserting G34/0, the long
    press had to fire, and starvation is no longer an available excuse. Nothing in this
    process can read the guest's clock directly, hence a bound.

    HOW THE TWO CLOCKS ARE PINNED TOGETHER. Let R be the host monotonic instant of release
    and E the (unknown) host instant at which the newest logged line was emitted. Then

        guest_at_R = last_ts_ms - guest_advance(R -> E)
                  >= last_ts_ms - (read_at - R)

    which is what this returns, offset by press_ts_ms. The step needs
    guest_advance(R -> E) <= read_at - R, and that holds under TWO conditions — note that
    E >= R is NOT one of them, since with E < R the guest advance is negative and the
    inequality is immediate:
      - read_at >= E. Then for E >= R: guest_advance(R -> E) <= host_advance(R -> E)
        = E - R <= read_at - R. Guaranteed by stamping read_at AFTER fh.read() returns, so
        every line in the tail was already written when the stamp was taken. Stamping it
        before the read would break exactly this and inflate the bound.
      - read_at >= R. Then for E < R the right-hand side is non-negative while the left is
        not. Guaranteed structurally: the only caller scans the log after the whole revert
        poll has run, tens of seconds past the release.

    The first bullet's middle step is the substantive one: the guest's millisecond clock can
    LAG host wall-clock but never lead it. Its ticks come from an emulated timer that QEMU
    drives off the host clock, and the QEMU invocation (conftest.py:1096-1110) passes no
    -icount, so there is no virtual clock that can jump ahead while the guest idles.
    Contention makes the guest MISS ticks — precisely the failure mode this whole file is
    about — and every millisecond missed makes this bound smaller, i.e. more forgiving.

    Three further approximations, all in the same acquitting direction:
      - `read_at` is when the tail was READ, not when its last line was emitted, so the
        subtraction removes more host time than it should.
      - `release_at_host` comes from press_button()'s own stamp taken before it bursts the
        release, i.e. the EARLIEST instant the press stopped being asserted, which again
        over-subtracts.
      - the guest's observation of the release can only be later than the host's release,
        which lengthens the true held window this underestimates.
    So a bound that still clears the threshold clears it with room to spare.
    """
    if markers["press_ts_ms"] is None or markers["last_ts_ms"] is None:
        return None
    host_elapsed_ms = (markers["read_at"] - release_at_host) * 1000.0
    return (markers["last_ts_ms"] - host_elapsed_ms) - markers["press_ts_ms"]


def _button_markers_since(offset, read_timeout=_SERIAL_SCAN_MAX_S):
    """Which config-button/factory-reset markers appeared in serial written since `offset`.

    Returns a dict of flags, or None when there is no usable log. None means "no verdict is
    possible", NOT "nothing happened"; the caller must not classify on it.

    THREE WAYS TO GET None, and the third is the subtle one.
      - `offset` is None: nothing could stat the log.
      - the file cannot be opened.
      - the log produced NOT ONE BYTE since `offset`. That is not a quiet device, it is a
        log that does not belong to the device under test — the case where this tree holds
        a stale build/qemu_test.log from an earlier local run while the tests are pointed
        at a real board with `--ip`. Classifying on it would read the stale file's silence
        as "the firmware never saw the press" and SKIP, hiding a genuine firmware defect
        behind an environment excuse; that is exactly the silent misclassification this
        must not do. The check is safe against a merely idle guest because the caller only
        gets here after polling GET /settings for _REVERT_POLL_MAX_S, and the firmware logs
        `settings_manager: Settings GET request` for every one of them — a live guest's
        tail cannot be empty by the time this returns.

    Re-reads on a bounded retry until _MARK_RESET_COMPLETED shows up or the deadline passes.
    That marker and not the long-press one, even though the long press happens first: the
    question every branch below turns on is whether the reset FINISHED, so waiting on the
    earlier marker would stop the clock before the decisive fact could appear. A transient
    OSError is retried for the same reason it is in 33_test_auth_settings.py:74-76 — a failed
    stat or read is a reason to look again, not to abandon the classification.

    Alongside the flags it returns the guest's own clock readings needed by the case-2
    verdict: `press_ts_ms` (the LAST press-event line's timestamp — every press edge restarts
    long_press_time_stamp at main/config_button.c:104, so the last one is the one that would
    have produced a long press), `last_ts_ms` (the newest timestamp anywhere in the tail) and
    `read_at` (time.monotonic() when that tail was read, which is what pins the two clocks
    together).
    """
    if offset is None:
        return None
    deadline = time.monotonic() + read_timeout
    while True:
        try:
            with open(_qemu_serial_log_path(), "r", errors="replace") as fh:
                fh.seek(offset)
                tail = fh.read()
        except OSError:
            tail = ""
        # STAMPED AFTER THE READ, and that ordering is load-bearing rather than tidy.
        # _guest_held_ms_after_press needs read_at >= the emission time of every line it is
        # about to see. Stamping first breaks that: any line the guest emits between the
        # stamp and fh.read() returning lands in `tail` with an emission time LATER than
        # read_at, the formula then subtracts less host time than really elapsed, and the
        # bound comes out too HIGH — i.e. biased toward accusing the firmware. On the node
        # this whole change is about, where one POST /settings took 15.88 s, a one-second
        # preemption between those two statements is an ordinary event. Stamping afterwards
        # makes read_at >= E true by construction and can only enlarge the subtrahend.
        read_at = time.monotonic()
        if not tail.strip():
            # Nothing at all yet, or nothing readable. Keep waiting rather than deciding,
            # and report "no verdict" only if it stays that way — see the docstring.
            if time.monotonic() >= deadline:
                return None
            time.sleep(0.5)
            continue
        press_ts_ms, last_ts_ms = _parse_log_timestamps(tail)
        found = {
            "press": _MARK_PRESS in tail,
            "long_press": _MARK_LONG_PRESS in tail,
            "reset_triggered": _MARK_RESET_TRIGGERED in tail,
            "reset_started": _MARK_RESET_STARTED in tail,
            "reset_completed": _MARK_RESET_COMPLETED in tail,
            "term_key_written": _MARK_TERM_KEY_WRITTEN in tail,
            "press_ts_ms": press_ts_ms,
            "last_ts_ms": last_ts_ms,
            "read_at": read_at,
        }
        if found["reset_completed"] or time.monotonic() >= deadline:
            return found
        time.sleep(0.5)


def _reread_term(api):
    """Re-read rs485_1.term over HTTP, for the branches that are about to convict.

    Both convicting branches fire on a marker the SCAN may have seen after the revert poll
    already gave up, which makes the poll's last reading stale rather than wrong. Failing on
    a stale value would manufacture the very accusation the rest of the classification exists
    to prevent, so the fact is re-established at the moment of the verdict.

    A TRANSPORT FAILURE HERE IS NOT A VERDICT. If this GET times out or the connection drops,
    nothing has been learned about the setting — and letting that surface as a raw traceback
    would put a red item on the board for a network hiccup, which is the failure class this
    whole classification removes. api_client.py:91-93 notes GET /settings is "occasionally
    >10 s" against a 30 s read leg, so the margin is real but not unbounded. It is narrow (the
    revert poll just made the same call repeatedly and succeeded), but narrow is not zero, and
    the term_key_written branch newly routes traffic through here that used to end in a skip.
    """
    try:
        resp = api.get_settings()
    except requests.exceptions.RequestException as exc:
        _skip_detector_disarmed(
            f"the serial log carried a marker that would have convicted the firmware, but "
            f"the confirming re-read of rs485_1.term never completed: "
            f"{type(exc).__name__}: {exc}. A verdict that accuses the firmware may not rest "
            f"on the stale reading the revert poll left behind, and no fresh one could be "
            f"obtained, so THIS RUN PROVES NOTHING either way."
        )
    assert resp.status_code == 200, (
        f"GET /settings returned {resp.status_code} while re-reading rs485_1.term to "
        f"confirm a factory-reset verdict"
    )
    return resp.json()["rs485_1"]["term"]


def _skip_detector_disarmed(reason):
    """End the item as SKIPPED: the environment prevented the press from being tested.

    WHY SKIP AND NOT FAIL. Each branch that reaches here has ruled out, as far as this run
    can, that the firmware ever got to finish the factory reset — so a red item would paint
    the build UNSTABLE for a busy CI node and teach people to ignore the gate.

    BUT BE HONEST ABOUT WHAT THAT REST ON. Every one of these branches fires on the ABSENCE
    of a marker, and absence alone does not separate a starved node from a firmware
    regression that stops the marker being emitted at all. Skipping is therefore a decision
    to under-report, and it is only defensible because each branch does something further:
      - the no-press branch is backed up elsewhere. 44_test_io_state_bus.py:140-144 hard
        asserts config_button_presses == before + 1, and is not @pytest.mark.reboot, so it
        runs BEFORE this item every time and reddens on a press-detection regression.
      - the press-but-no-long-press branch consults the guest's OWN clock first
        (_guest_held_ms_after_press). When that clock shows the guest DID live through the
        threshold while the button was held, the branch FAILS instead. It still skips in two
        distinguishable situations, and only the first is an acquittal: the guest
        demonstrably did not live through the threshold, or the bound could not be computed
        at all because the log carried no usable timestamps. The second is "unknown", not
        "not the firmware's fault", and the skip text says which of the two applied.
      - the triggered-but-not-completed branch is the narrowest it can be made: a reset that
        already logged writing 485_term_1 convicts via _MARK_TERM_KEY_WRITTEN rather than
        landing here, so what remains is a reset that stopped BEFORE that key. That case has
        no independent cover, which is why its message names the alternative explicitly.

    Nothing that accuses the firmware is weakened for any of it: a completed reset that left
    the setting unchanged, and a long press that should have fired on the guest's own clock
    and did not, both still fail hard.

    WHY THE MARKER, AND WHY IT IS ALSO PRINTED. A skip nobody counts is how a test quietly
    stops running forever. Under this suite's addopts (api_tests/pytest.ini:5 — `-v -s
    --tb=short`, no `-r`) pytest truncates the reason in its own result line to
    `SKIPPED (DETECTOR-DISARME...)`, cutting the marker mid-word, so the print is what keeps
    the full banner greppable in the Jenkins console log; the untruncated reason also lands
    in build/qemu_test_report.xml as <skipped message="...">. Not in build/qemu_test.log —
    that is the GUEST's serial output and holds nothing this process prints.
    """
    banner = f"{_DISARMED_MARKER}: {reason}"
    print(f"\n{banner}")
    pytest.skip(banner)


def test_eth_led_reacts_to_traffic(api):
    """E05 (Eth LED) must change at least once while Ethernet traffic flows.

    Ethernet is connected in QEMU (10.0.2.15). The indication task blinks the
    Eth LED on ifinoctets activity, so issuing a burst of HTTP GETs (which is
    Ethernet RX from the firmware's point of view) should toggle E05. We pump
    the bus while generating traffic and count edges on E05.
    """
    with IoBus() as bus:
        # Generate Ethernet traffic in a loop while pumping the bus to catch the
        # LED toggles. The indication task only blinks when ifinoctets changes
        # between samples, so we keep the traffic flowing throughout the window.
        start = len(bus.events)
        deadline = time.monotonic() + 6.0
        edges = 0
        while time.monotonic() < deadline:
            resp = api.get_info()
            assert resp.status_code == 200, f"GET /info returned {resp.status_code}"
            bus.pump(0.2)
            edges = sum(
                1 for (pin, _level) in bus.events[start:] if pin == "E05"
            )
            if edges >= 1:
                break
        assert edges >= 1, (
            f"Expected Eth LED (E05) to change at least once while Ethernet "
            f"traffic flowed, saw {edges} edges. indication.c eth LED control "
            "may not be running."
        )


def test_wifi_led_present(api):
    """E04 (WiFi LED) must be present in the dump with a valid level.

    WiFi is MOCKED in QEMU (wifi_qemu_mock.c): there is no real STA/AP netif
    carrying traffic, so the WiFi LED is not expected to blink and asserting it
    toggles would be flaky and meaningless. We only assert the pin exists in the
    full dump and holds a valid level (0 or 1), proving leds_control wired it.
    """
    with IoBus() as bus:
        assert "E04" in bus.state, (
            f"WiFi LED (E04) missing from the dump; leds_control did not wire "
            f"it. Available pins: {sorted(bus.state)}"
        )
        level = bus.get("E04")
        assert level in (0, 1), (
            f"WiFi LED (E04) present but holds an invalid level {level!r}; "
            f"expected 0 or 1. Available pins: {sorted(bus.state)}"
        )


@pytest.mark.reboot
# Overrides the suite-wide 180 s (api_tests/pytest.ini:10), which this item outgrew. THE
# ONLY budget derivation for this item; every other comment that mentions a term of it
# points here.
#
# Derived from the CLIENT-SIDE ceilings rather than from what has been observed, because a
# budget sized on observation expires on the first node worse than the one we measured — and
# expiring here converts a careful DETECTOR-DISARMED skip into a red timeout, i.e.
# reintroduces the exact false accusation this commit removes.
#
# CALL PHASE. api_client.py:94/:98 pass a SCALAR timeout=30 to requests, which bounds
# connect and read SEPARATELY, so the strict per-call ceiling is 60 s, not 30. The 30 s
# below is the READ leg only, and taking it as the whole call is justified the same way
# conftest.py:1260-1263 justifies its own asymmetric split: connect is a loopback TCP
# handshake to a QEMU hostfwd port, "either immediate or never", so the connect leg cannot
# quietly consume seconds. (Strictly scalar this item would need ~417 s: the six 30 s calls
# below — three before the press, the poll's trailing GET, the re-read and the restore —
# doubled to 60 s each, plus the 57 s of terms that are not HTTP calls. That regime needs a
# loopback handshake to hang for 30 s, which is not a thing that happens.)
#     3 x 30 s   the GET/POST/GET before the press
#          1 s   IoBus() construction — its __init__ ends with pump(1.0)
#         14 s   the press: 12 s hold + up to 2 s release confirm
#          2 s   the pump(2.0) that catches the blink tail
#   30 + 30 s    the revert poll (_REVERT_POLL_MAX_S, then one last GET that may start just
#                under the deadline and still run to completion)
#         10 s   the serial scan (_SERIAL_SCAN_MAX_S)
#         30 s   the re-read on a convicting branch
#         30 s   the restore in `finally`
#     = 237 s
#
# TEARDOWN PHASE, which counts too: pytest_timeout bounds setup + call + teardown unless
# func_only is set, and it is not. This item is the LAST of the run — pytest_collection_
# modifyitems orders `baseline + body + final + reboot` (conftest.py:102-116) and this is
# the last reboot file — so session and module teardowns land here:
#       41.2 s   the module-scoped _restore_rs485_settings (conftest.py:1370), whose ceiling
#                conftest.py:1266-1269 computes as 2 ports x 20.1 s + _RS485_RESTORE_SETTLE_S
#          4 s   _uart_leak_guard probing both UART chardevs at 2.0 s each (conftest.py:1621)
#          5 s   qemu_process's SIGTERM + proc.wait(timeout=5) (conftest.py:1144-1146)
#     = ~51 s
#
# 237 + 51 = ~288 s. 360 leaves ~72 s (25%) of headroom on top of a ceiling that already
# stacks every term at its worst simultaneously. A healthy run finishes in a few seconds and
# pays none of it.
@pytest.mark.timeout(360)
def test_factory_reset_long_press(api):
    """A >5 s config-button hold must factory-reset settings to defaults.

    Destructive: wipes all settings. Procedure:
      1. Save current settings; set a recognizable non-default value
         (rs485_1.term=False; firmware default is True) and confirm it took.
      2. Hold the config button LOW for _FACTORY_RESET_HOLD_S (2.4x the firmware's
         5 s threshold — see the constant) to trigger the long-press factory reset.
      3. Assert rs485_1.term reverted to its firmware default True — proving
         factory_reset() ran. As a secondary signal, observe an E07 fast-blink
         burst (indication_status_led_blink_n_times(200ms, 5)) during the press.
      4. If it did NOT revert, read the firmware's own serial markers to decide
         whether the firmware is at fault at all — see step 4 below.
    Restores the originally-saved settings in finally (best-effort).
    """
    resp = api.get_settings()
    assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
    original_settings = resp.json()

    try:
        # Step 1: set a recognizable non-default value and confirm it took.
        resp = api.update_settings({"rs485_1": {"term": False}})
        assert resp.status_code == 200, (
            f"POST rs485_1.term=False returned {resp.status_code}"
        )
        assert resp.json().get("success") is True, (
            f"POST rs485_1.term=False not successful: {resp.text}"
        )
        resp = api.get_settings()
        assert resp.status_code == 200
        assert resp.json()["rs485_1"]["term"] is False, (
            "Precondition failed: rs485_1.term did not become False before reset"
        )

        # Step 2: long-press triggers the factory reset. Count E07 edges during the
        # hold as a secondary signal of the fast-blink burst.
        #
        # The serial baseline is taken BEFORE the press, so step 4 can only ever see
        # markers this press produced — earlier tests in this suite press the same
        # button, and scanning the whole log would classify their markers as ours.
        with IoBus() as bus:
            edges_start = len(bus.events)
            serial_offset = _serial_log_offset()
            # press_button() hands back the monotonic instant at which it stopped asserting
            # the press, which is what _guest_held_ms_after_press needs.
            #
            # WHY NOT RECONSTRUCT IT as press_start + _FACTORY_RESET_HOLD_S, which is what
            # this did before. Not because of the sub-second geometry — that actually
            # favoured the old form, since the helper bursts the press edge BEFORE starting
            # its hold deadline, so start + hold lands ~30-50 ms EARLIER than the true
            # release and was marginally more acquitting. The real defect was the clock: the
            # helper measured its hold on time.time(), so a forward CLOCK_REALTIME step
            # released the button immediately while a monotonic start + 12.0 still pointed
            # 12 s into the future. That gap goes straight into the bound as a shrunken
            # correction term — an inflated bound, i.e. an accusation — and the same step is
            # what pushes the run into the branch that consults the bound in the first place.
            # Taking the helper's own stamp removes the second clock entirely, and it also
            # survives any future change to the send sequence.
            release_at_host = bus.press_button(hold_s=_FACTORY_RESET_HOLD_S)
            # Pump a bit more to catch the tail of the blink burst.
            bus.pump(2.0)
            e07_edges = sum(
                1 for (pin, _level) in bus.events[edges_start:] if pin == "E07"
            )

        # Step 3: authoritative assertion — the setting reverted to its default.
        # The button task samples G34 periodically and the callback runs
        # synchronously, so poll for the revert.
        deadline = time.monotonic() + _REVERT_POLL_MAX_S
        term_after = None
        while time.monotonic() < deadline:
            resp = api.get_settings()
            assert resp.status_code == 200, f"GET /settings returned {resp.status_code}"
            term_after = resp.json()["rs485_1"]["term"]
            if term_after is True:
                break
            time.sleep(0.5)

        if term_after is not True:
            # Step 4: WHOSE FAULT IS IT? "factory_reset() may not have run" — the verdict
            # this test used to give — accuses the firmware of something a loaded CI node
            # causes routinely, because the host cannot see whether the guest ever
            # observed the press. The firmware says so itself in serial, so ask it.
            markers = _button_markers_since(serial_offset)

            assert markers is not None, (
                f"Expected rs485_1.term to revert to firmware default True after the "
                f"{_FACTORY_RESET_HOLD_S:.0f} s long-press factory reset, got "
                f"{term_after}. The QEMU serial log ({_qemu_serial_log_path()}) is "
                f"unusable here — missing, unreadable, or silent since the baseline taken "
                f"before the press, which is what a stale log looks like when the device "
                f"under test is a real board reached over --ip. Without it this run cannot "
                f"tell a firmware defect from a starved guest that never observed the "
                f"press, so treat this verdict as UNCLASSIFIED rather than as a firmware "
                f"accusation, and re-run against a QEMU whose serial log this process can "
                f"read to get a real one."
            )

            # Case 4 first, because it is the only one that convicts on the reset itself.
            # _MARK_RESET_COMPLETED is the only marker that proves
            # setting_items_set_defaults() returned; the other three mean "about to start".
            if markers["reset_completed"]:
                # RE-READ before convicting. The scan can watch the reset finish AFTER the
                # revert poll gave up — 55 priority-2 NVS writes against our priority-5
                # polling — in which case term_after is merely stale and there is no defect
                # at all. One more GET costs a few seconds on a path that is already
                # failing, and without it this branch would manufacture the exact false
                # accusation the rest of this block exists to prevent.
                term_after = _reread_term(api)
                assert term_after is True, (
                    f"Expected rs485_1.term to revert to firmware default True after the "
                    f"long-press factory reset, got {term_after} — and the firmware's own "
                    f"serial says the reset RAN TO COMPLETION: it logged "
                    f"{_MARK_RESET_COMPLETED!r}, which main.c:52 only reaches after "
                    f"setting_items_set_defaults() returned ESP_OK having written all 55 "
                    f"keys, KEY_485_TERM_1 among them. Re-read after that marker appeared "
                    f"and it is still {term_after}, so this is not a slow node. FIRMWARE "
                    f"DEFECT in setting_items_set_defaults() / settings_update()."
                )

            elif markers["term_key_written"]:
                # The loop may still be running, or may have died at a later key, but it
                # already reported writing THIS key successfully — which is all this test
                # reads back. Same re-read discipline as above, and same conviction.
                term_after = _reread_term(api)
                assert term_after is True, (
                    f"Expected rs485_1.term to revert to firmware default True after the "
                    f"long-press factory reset, got {term_after} — and the firmware's own "
                    f"serial says it WROTE THAT KEY: it logged "
                    f"{_MARK_TERM_KEY_WRITTEN!r}, which setting_items.c:274 only reaches "
                    f"when storage_iface->write_str() returned ESP_OK for 485_term_1 (a "
                    f"failure would have logged 'Failed to set default for' at :276 "
                    f"instead). Re-read after that marker appeared and it is still "
                    f"{term_after}, so this is not a slow node and not an unfinished loop: "
                    f"the write the firmware claims to have made is not visible over the "
                    f"API. FIRMWARE DEFECT in setting_items_set_defaults() / the settings "
                    f"read path."
                )

            elif markers["long_press"] or markers["reset_triggered"] or markers["reset_started"]:
                # Case 3: the trigger fired but the work never finished inside our window.
                # Not convictable: setting_items_set_defaults() writes all 55 keys from
                # config_button_task at priority 2 (config_button.c:14), which the IDF httpd
                # task at priority 5 — serving this test's own polling — preempts. On a node
                # where a single POST /settings has measured 15.88 s, not finishing 55
                # writes inside the budget is an ordinary outcome.
                _skip_detector_disarmed(
                    f"the factory reset was TRIGGERED but the firmware never logged "
                    f"{_MARK_RESET_COMPLETED!r} within {_SERIAL_SCAN_MAX_S:.0f} s of the "
                    f"revert poll giving up (long_press={markers['long_press']}, "
                    f"reset_triggered={markers['reset_triggered']}, "
                    f"reset_started={markers['reset_started']}): "
                    f"setting_items_set_defaults() writes 55 NVS keys from a priority-2 "
                    f"task that this test's own priority-5 polling preempts, so on a loaded "
                    f"node it can still be mid-loop. rs485_1.term is {term_after}, which is "
                    f"what an unfinished reset looks like. THIS RUN PROVES NOTHING about "
                    f"whether the reset would have completed — but note the same pattern "
                    f"would appear if setting_items_set_defaults() hung or aborted partway, "
                    f"so if this skip recurs on an IDLE node, treat it as a firmware lead "
                    f"and look at main/setting_items.c:261-283."
                )

            elif not markers["press"]:
                # Case 1: the firmware never logged a press at all. The G34 datagrams went
                # out over QEMU usermode-NAT UDP and either never arrived or were never
                # sampled. Nothing about factory_reset() was exercised. Independently
                # covered: 44_test_io_state_bus.py:140-144 asserts the press counter
                # advances and runs before this item, so a real press-detection regression
                # reddens there rather than hiding here.
                _skip_detector_disarmed(
                    f"the firmware never logged {_MARK_PRESS!r} while the host held "
                    f"the config button LOW for {_FACTORY_RESET_HOLD_S:.0f} s, so the "
                    f"press never reached the guest (lossy slirp UDP, or a guest too "
                    f"starved to sample G34) and the long-press factory reset was "
                    f"never exercised. rs485_1.term is still {term_after}, as expected "
                    f"when no reset was triggered. THIS RUN PROVES NOTHING about the "
                    f"firmware either way; a genuine press-detection regression is "
                    f"caught by 44_test_io_state_bus.py, which runs earlier."
                )

            else:
                # Case 2: the press was observed but no long press followed. The firmware
                # measures from the moment its button task SEES the press
                # (main/config_button.c:104), not from when the host sent the datagram, so a
                # guest that noticed the press late never accumulates the threshold before
                # the host releases. That is what build #45 hit at the old 6 s hold — but
                # "no marker" on its own cannot tell that starved node from a regression in
                # the threshold passed at main/main.c:235 or in the pending_long_press
                # handling at config_button.c:105/111/120, and this is the suite's only
                # long-press test. So ask the guest's own clock before letting the
                # environment take the blame.
                guest_held_ms = _guest_held_ms_after_press(markers, release_at_host)
                verdict_threshold_ms = (
                    _FW_LONG_PRESS_THRESHOLD_S + _LONG_PRESS_VERDICT_MARGIN_S) * 1000.0

                if guest_held_ms is not None and guest_held_ms >= verdict_threshold_ms:
                    # The guest lived through more of ITS OWN clock with the button held
                    # than the threshold needs, and still never fired. Starvation cannot
                    # explain that: the firmware would have had to sample at least once in
                    # that window with pending_long_press still set, and pending_long_press
                    # is only cleared by the button task debouncing a release that had not
                    # happened yet.
                    assert term_after is True, (
                        f"Expected rs485_1.term to revert to firmware default True after "
                        f"the long-press factory reset, got {term_after}. The firmware "
                        f"logged {_MARK_PRESS!r} but never {_MARK_LONG_PRESS!r} — and by "
                        f"its OWN clock at least {guest_held_ms / 1000.0:.1f} s elapsed "
                        f"between that press and the host releasing the button, against a "
                        f"{_FW_LONG_PRESS_THRESHOLD_S:.0f} s threshold "
                        f"(+{_LONG_PRESS_VERDICT_MARGIN_S:.0f} s margin). The guest was NOT "
                        f"too starved to reach the threshold, so this is not an environment "
                        f"failure. FIRMWARE DEFECT in the long-press path: check the "
                        f"threshold passed at main/main.c:235 and the pending_long_press "
                        f"handling at main/config_button.c:105/111/120."
                    )

                # TWO DIFFERENT VERDICTS SHARE THIS SKIP, and the text must not blur them.
                # With a bound, the guest is acquitted on its own clock. Without one, the
                # answer is simply unknown — no acquittal, just no evidence either way.
                held_note = (
                    "its own clock could not be read from the log at all, so how much of "
                    "the hold it lived through is UNKNOWN rather than acquitted"
                    if guest_held_ms is None
                    else f"by its own clock only {guest_held_ms / 1000.0:.1f} s elapsed "
                         f"between that press and the release, under the "
                         f"{_FW_LONG_PRESS_THRESHOLD_S:.0f} s threshold "
                         f"(+{_LONG_PRESS_VERDICT_MARGIN_S:.0f} s margin)"
                )
                _skip_detector_disarmed(
                    f"the firmware logged {_MARK_PRESS!r} but never "
                    f"{_MARK_LONG_PRESS!r}: it saw the press, and {held_note} — even "
                    f"though the host "
                    f"held the button LOW for {_FACTORY_RESET_HOLD_S:.0f} s. The firmware "
                    f"stamps the hold when its button task SAMPLES the press "
                    f"(config_button.c:104), not when the host sent it, so a guest that "
                    f"fell behind never accumulates the threshold: a starved node, not a "
                    f"broken factory reset. rs485_1.term is still {term_after}, as expected "
                    f"when the threshold was never reached. THIS RUN PROVES NOTHING about "
                    f"the firmware either way — though the same pattern would also appear "
                    f"if the threshold at main/main.c:235 or the pending_long_press "
                    f"handling regressed, so if this skip recurs on an IDLE node, treat it "
                    f"as a firmware lead."
                )

        # Secondary signal: the factory-reset fast-blink burst should have caused
        # several E07 edges. This is purely informational and must NOT fail a
        # correct factory reset: the E07 fast-blink is emitted over lossy QEMU
        # usermode-NAT UDP and can be missed entirely. The rs485_1.term revert
        # above is the authoritative proof.
        if e07_edges < 1:
            print(
                f"Warning: saw no E07 (Status LED) edges during/after the "
                f"factory-reset press (e07_edges={e07_edges}); E07 fast-blink "
                "can be lost over UDP and is non-authoritative."
            )
    finally:
        # Best-effort restore of the originally-saved settings so the device is
        # left configured. Do not assert here to preserve any test exception.
        #
        # AND DO NOT RAISE HERE EITHER. An exception raised inside `finally` REPLACES the one
        # in flight, so an unwrapped POST would destroy whatever verdict the body reached —
        # including the DETECTOR-DISARMED skips and the worded FIRMWARE DEFECT asserts — and
        # replace it with a raw traceback from this line. This block is older than those
        # verdicts and was harmless while every path out of the body was already an
        # assertion; it is the classification added above that makes it load-bearing, and in
        # particular _reread_term's "a network hiccup must not redden the item" only holds if
        # the hiccup does not also reach this call. Same convention as conftest.py:1446,
        # which wraps its own restore so teardown cannot mask a test failure.
        #
        # Only the TRANSPORT error is swallowed. A device that is genuinely dead still goes
        # red, via the polling loop and the status-code asserts above, none of which catch.
        try:
            restore = api.update_settings(original_settings)
        except requests.exceptions.RequestException as exc:
            print(f"Failed to restore settings after factory reset: {exc!r}")
        else:
            if restore.status_code != 200:
                print(
                    f"Failed to restore settings after factory reset: "
                    f"HTTP {restore.status_code}"
                )
