"""Helper for the QEMU virtual IO state bus (guest UDP port 5570, forwarded to this
slot's IO-bus host port — see qemu_ports.IO_BUS_UDP_PORT).

The QEMU-only build exposes firmware GPIO/expander/button state over a tiny
UDP protocol so host-side tests can observe and drive "hardware" pins that
otherwise have no presence on the host.

Wire protocol (matches main/qemu/virtual_io_qemu.c):
    Each datagram is exactly 5 ASCII bytes: ``<T><NN>/<X>``
        T  : 'E' = expander pin, 'G' = native ESP32 GPIO level,
             'D' = native GPIO direction, 'V' = native GPIO direction violation
        NN : zero-padded 2-digit pin number
        '/': literal separator, always at index 3
        X  : for 'E'/'G' the RAW physical pin level ('0'/'1'); for 'D' the
             direction ('1' OUTPUT, '0' INPUT); for 'V' the violation cause
             ('0' host wrote an OUTPUT pin, '1' firmware configured an
             input-only pad as an OUTPUT, '2' host operated an UNCONFIGURED pin)
    A single optional trailing '\\n' is tolerated.

Tracked signals:
    Expander register bits E00..E15 (E00 RS485-1 term, E01 RS485-2 term,
    E02 RS485-1 pull-up, E03 RS485-2 pull-up, E04 WiFi LED [inverted],
    E05 Eth LED [inverted], E06 VOut, E07 Status LED, E08 MIO; E09..E15 unused).
    Native GPIO G04 (RS485-1 direction), G15 (RS485-2 direction),
    G34 (config button input). Each native pin also carries a DIRECTION
    (D<NN>: 1=OUTPUT, 0=INPUT), DERIVED from the firmware's real ESP-IDF gpio
    config (no hardcoded defaults), and may emit a VIOLATION (V<NN>) if a write
    breaks the model (host driving an OUTPUT, firmware configuring an input-only
    pad as an OUTPUT, or the host operating an UNCONFIGURED pin).

Direction of traffic:
    TX (guest -> host): one record per tracked-output change.
    RX (host -> guest): ``G34/0`` = button pressed (active LOW),
    ``G34/1`` = released. Other well-formed records update the firmware shadow
    but only G34 feeds firmware logic.

NAT peer-learning (critical):
    QEMU usermode networking only NATs host->guest, so the guest cannot send
    unsolicited datagrams. It learns the host address from the source of every
    received datagram and replies only to that last-known peer. So a client
    MUST send a datagram first to "subscribe", and resend periodically (~every
    1 s) to keep the usermode-NAT UDP mapping warm. On a new/changed peer the
    guest immediately sends a FULL dump of every tracked signal; because the
    peer is keyed on (address, source port), a fresh UDP socket always counts
    as a new peer and gets a fresh full dump.

Stdlib only — no external dependencies.
"""

import socket
import time

import qemu_ports

# 5-byte ASCII record layout: T NN '/' L
RECORD_LEN = 5
SEP_INDEX = 3
LEVEL_INDEX = 4


class IoBus:
    """Client for the QEMU virtual IO state bus (guest UDP 5570).

    On construction it opens a fresh UDP socket, subscribes (which makes the
    guest treat it as a new peer and send a full dump), and absorbs that dump
    so ``state`` is populated before the caller does anything. Usable as a
    context manager; remember to ``close()`` (or use ``with``) so the socket
    is released.

    Attributes:
        state: dict mapping pin name (e.g. "E07", "G34", "D04", "V15") to its
            latest value. For 'E'/'G' the value is the level (0/1); for 'D' the
            direction (1 OUTPUT / 0 INPUT); for 'V' the last violation cause.
        events: ordered list of (pin, value) tuples, one per received record.
        violations: ordered list of (gpio_num, cause) tuples (e.g. (4, 0))
            appended whenever a ``V<NN>/<c>`` record arrives. ``c=0`` host wrote
            an OUTPUT pin; ``c=1`` firmware configured an input-only pad as an
            OUTPUT; ``c=2`` the host operated an UNCONFIGURED pin. Empty in
            normal operation.
    """

    HOST = qemu_ports.HOST
    PORT = qemu_ports.IO_BUS_UDP_PORT  # UDP IO-bus host port for this run's slot

    # Resend the subscribe datagram at least this often (seconds) to keep the
    # QEMU usermode-NAT UDP mapping warm so TX records keep flowing.
    SUBSCRIBE_INTERVAL_S = 1.0

    def __init__(self, recv_timeout=0.1):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.settimeout(recv_timeout)
        self.state = {}
        self.events = []
        self.violations = []
        self._last_subscribe = 0.0
        self._subscribe()
        # Absorb the initial full dump triggered by the fresh-peer subscribe.
        self.pump(1.0)

    # --- internals ------------------------------------------------------------

    def _subscribe(self):
        """Send a no-op datagram so the guest learns/refreshes this peer.

        ``G34/1`` (button released) is the safest payload: it is the firmware
        idle/default state, so it cannot accidentally trigger a button press.
        """
        self._sock.sendto(b"G34/1", (self.HOST, self.PORT))
        self._last_subscribe = time.monotonic()

    @staticmethod
    def _parse(data):
        """Parse one 5-byte record into (pin, value), or None if malformed.

        Accepts all four record types: 'E'/'G' (level), 'D' (direction), 'V'
        (violation cause). ``pin`` keeps the type prefix (e.g. "E07", "G34",
        "D04", "V15") and ``value`` is the index-4 digit. For 'E'/'G'/'D' the
        value is strictly 0/1; for 'V' the cause is a single digit (0 host wrote
        OUTPUT, 1 firmware drove an input-only pad, 2 host operated an
        uninitialized pin). Tolerates a single trailing newline. Parsing is
        positional to match the encoder.
        """
        if len(data) == RECORD_LEN + 1 and data[RECORD_LEN:RECORD_LEN + 1] == b"\n":
            data = data[:RECORD_LEN]
        if len(data) != RECORD_LEN:
            return None
        if data[SEP_INDEX:SEP_INDEX + 1] != b"/":
            return None
        type_char = chr(data[0])
        if type_char not in ("E", "G", "D", "V"):
            return None
        if not data[1:3].isdigit():
            return None
        value_byte = data[LEVEL_INDEX:LEVEL_INDEX + 1]
        if type_char == "V":
            # Violation cause is any single digit (0/1/2).
            if not value_byte.isdigit():
                return None
        elif value_byte not in (b"0", b"1"):
            return None
        pin = f"{type_char}{data[1:3].decode('ascii')}"
        value = int(chr(data[LEVEL_INDEX]))
        return pin, value

    # --- public API -----------------------------------------------------------
    #
    # EVERY DEADLINE IN THIS CLASS IS time.monotonic(), INCLUDING _last_subscribe, and they
    # were moved off time.time() together on purpose: a class with two clock bases is worse
    # than one with either, because the mistakes it enables (subtracting one from the other)
    # are silent. A CLOCK_REALTIME step — NTP correction, resume from suspend, container
    # clock fixup — moves the wall clock in either direction, and each direction broke a
    # different loop here. Forward: press_button's hold ended early, shortening a press whose
    # entire job is to clear a 5 s firmware threshold, which then reads as a firmware fault
    # (see 47_test_io_indication.py). Backward: pump() and wait_for() spun for the size of
    # the step at one recv timeout per iteration, burning a caller's pytest-timeout budget on
    # a clock event. monotonic() cannot step in either direction.

    def pump(self, duration):
        """Receive records for ``duration`` seconds, keeping the NAT warm.

        Re-subscribes whenever the subscribe interval has elapsed so the guest
        keeps sending TX records to this peer. Every parsed record updates
        ``state`` and is appended to ``events``. A ``V<NN>/<c>`` (violation)
        record is additionally appended to ``violations`` as ``(gpio_num, cause)``.
        """
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if time.monotonic() - self._last_subscribe > self.SUBSCRIBE_INTERVAL_S:
                self._subscribe()
            try:
                data, _ = self._sock.recvfrom(64)
            except socket.timeout:
                continue
            record = self._parse(data)
            if record is not None:
                pin, value = record
                self.state[pin] = value
                self.events.append(record)
                if pin[0] == "V":
                    # (gpio_num:int, cause:int) — e.g. (4, 0). cause: 0 host wrote
                    # OUTPUT, 1 firmware wrote INPUT, 2 operate uninitialized pin.
                    self.violations.append((int(pin[1:]), value))

    def get(self, pin):
        """Return the latest known value of ``pin``, or None if never seen."""
        return self.state.get(pin)

    def had_violation(self):
        """True if any direction-violation (``V``) record has been received."""
        return len(self.violations) > 0

    def wait_for(self, pin, level, timeout=5.0):
        """Block until ``pin`` reaches ``level``. Returns True on success.

        Level-triggered: returns True if the pin is already at ``level``, so a
        caller needing to observe a transition must reset the precondition
        itself. Pumps the socket once before the first check so the decision
        reflects current state, not a leftover from a prior dump/event.
        """
        deadline = time.monotonic() + timeout
        self.pump(0.1)
        while time.monotonic() < deadline:
            if self.state.get(pin) == level:
                return True
            self.pump(0.3)
        return self.state.get(pin) == level

    def count_edges(self, pin, duration):
        """Count value changes (any edge) of ``pin`` over ``duration`` seconds.

        Counts records received for ``pin`` while pumping; the firmware only
        emits a record when a tracked output actually changes, so each emitted
        record is one edge.
        """
        start = len(self.events)
        self.pump(duration)
        return sum(1 for (recorded_pin, _level) in self.events[start:]
                   if recorded_pin == pin)

    def press_button(self, hold_s=0.4):
        """Simulate a short config-button press (active LOW on G34).

        Holds G34 LOW for ``hold_s`` seconds, then releases it (G34 HIGH). The
        firmware debounces the button over 50 ms and samples it every 10 ms, so
        the hold must comfortably exceed the debounce window; 0.4 s is safe.

        Returns the ``time.monotonic()`` stamp at which the press stopped being
        asserted, for callers that need to reason about how long the firmware
        could have seen the button down. Callers that do not care may ignore it.

        Robustness over QEMU usermode-NAT UDP (which can drop datagrams):
          - Each level transition is sent as a small burst, not a single
            datagram, so a dropped packet does not lose the edge.
          - The LOW state is resent steadily during the hold.
          - Crucially, this does NOT call ``pump()`` while holding: pump may
            re-subscribe with ``G34/1`` (release) and cancel the press mid-hold.
          - After release, it self-confirms: a dropped release datagram would
            otherwise leave the firmware's global G34 shadow stuck LOW for every
            later test. Pumping here is now safe (re-subscribe sends ``G34/1``,
            also a release) so we pump and re-send ``G34/1`` until the firmware
            reports ``G34 == 1`` or a short timeout elapses.

        Keep ``hold_s`` well under 5 s unless a factory reset is the POINT of the
        call: the firmware fires its long-press callback once the hold it OBSERVES
        reaches 5 s (main/main.c:39 CONFIG_BTN_FACTORY_RESET_HOLD_TIME_MS), which
        wipes all settings to defaults. The default 0.4 s is nowhere near it.

        One caller deliberately exceeds it — 47_test_io_indication.py::
        test_factory_reset_long_press holds for 12 s to trigger that reset on
        purpose, and needs the wide margin because the firmware measures the hold
        from when its button task SAMPLES the press, not from when this datagram
        was sent. So the rule above is "do not trip the reset by accident", not
        "5 s is an upper bound on hold_s".
        """
        # Burst the press edge to beat UDP loss.
        for _ in range(3):
            self._sock.sendto(b"G34/0", (self.HOST, self.PORT))
            time.sleep(0.01)
        # Hold LOW steadily without any interleaved release/re-subscribe. Monotonic for the
        # reason given at the top of the public API — this is the loop where a forward
        # wall-clock step did the most damage, cutting the hold short and getting the
        # resulting missed long press blamed on the firmware.
        deadline = time.monotonic() + hold_s
        while time.monotonic() < deadline:
            self._sock.sendto(b"G34/0", (self.HOST, self.PORT))
            time.sleep(0.02)
        # EARLIEST instant at which the press stopped being asserted: the hold loop has
        # stopped resending G34/0 and the release burst has not gone out yet. Returned so
        # a caller reasoning about how long the firmware saw the button down uses the
        # helper's own clock instead of reconstructing it as start + hold_s, which would
        # miss the burst above, the loop's overshoot past the deadline, and any future
        # change to the sequence. Earliest rather than latest on purpose: the firmware
        # keeps seeing LOW until a G34/1 actually arrives, so the true release is at or
        # after this stamp, and a caller subtracting from it errs toward a shorter
        # observed hold.
        release_at = time.monotonic()
        # Burst the release edge.
        for _ in range(3):
            self._sock.sendto(b"G34/1", (self.HOST, self.PORT))
            time.sleep(0.02)
        # Self-confirm the release so a lost G34/1 cannot leave the firmware
        # shadow stuck LOW. Pumping is safe now: re-subscribe also sends G34/1.
        confirm_deadline = time.monotonic() + 2.0
        while time.monotonic() < confirm_deadline:
            self.pump(0.1)
            if self.state.get("G34") == 1:
                break
            self._sock.sendto(b"G34/1", (self.HOST, self.PORT))
        return release_at

    def send_raw(self, payload):
        """Send a raw datagram to the bus from THIS peer's socket.

        ``payload`` may be ``bytes`` or ``str`` (encoded ASCII). Lets a test
        inject an arbitrary record (e.g. a ``G04/0`` write the firmware should
        reject as a direction violation) on the same socket the bus listens on,
        so any reply/violation comes back to this peer. Call ``pump()`` after to
        absorb the response.
        """
        if isinstance(payload, str):
            payload = payload.encode("ascii")
        self._sock.sendto(payload, (self.HOST, self.PORT))

    def close(self):
        """Close the underlying UDP socket."""
        self._sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
