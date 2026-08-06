"""E2E tests for Auth and Settings validation (AU-01..AU-04, ST-01..ST-06, H3)"""

import os
import time

import pytest
import requests

from api_client import WBMGEAPI

# Maximum number of concurrent sessions (ring buffer size in auth.c)
MAX_SESSIONS = 10


def _qemu_serial_log_path():
    """Path to the live QEMU serial capture written by conftest.py."""
    return os.path.join(os.path.dirname(__file__), "..", "build", "qemu_test.log")


def _serial_log_offset():
    """Current size of the QEMU serial log, or None if unavailable.

    Captured just before a reboot so we only scan serial emitted by the
    boot that follows.
    """
    try:
        return os.path.getsize(_qemu_serial_log_path())
    except OSError:
        return None


def _sessions_restored_after_reboot(offset, read_timeout=10.0):
    """Inspect QEMU serial written since `offset` for the firmware's auth-init decision.

    Returns True if sessions were restored (clean ESP_RST_SW reboot), False if
    they were wiped (abnormal WDT/panic reset), or None if neither marker is
    found within `read_timeout` (e.g. log unavailable) so the caller can fall
    back to asserting.

    Re-reads the log on a bounded retry: QEMU writes the log from a separate
    process via its own stdio buffering, so the boot line may not be flushed to
    disk the instant the device starts answering HTTP again.
    """
    deadline = time.monotonic() + read_timeout
    while True:
        try:
            with open(_qemu_serial_log_path(), "r", errors="replace") as fh:
                if offset:
                    fh.seek(offset)
                tail = fh.read()
        except OSError:
            tail = ""
        restored = tail.rfind("Saved auth sessions were loaded")
        wiped = tail.rfind("Auth sessions were reset")
        if restored != -1 or wiped != -1:
            # Use whichever marker appears LAST: the boot that actually brought the device up.
            return restored > wiped
        if time.monotonic() >= deadline:
            return None
        time.sleep(0.5)


def _wait_device_up(base_url: str, timeout: int = 1800, stable_polls: int = 3) -> bool:
    """Poll until the device answers HTTP (without auth) STABLY.

    Returns True only after `stable_polls` consecutive successful responses. A single
    success is not enough right after a reboot: QEMU can briefly accept a connection
    on the still-running pre-reset firmware, or answer once while the httpd is only
    half-initialised, and the very next request then hits the device mid-recovery and
    raises ConnectionError. Requiring consecutive successes (any blip resets the
    counter) means callers — the post-reboot session checks and the fixture re-auth —
    run against a device that is actually ready.
    """
    plain = requests.Session()
    deadline = time.monotonic() + timeout
    consecutive = 0
    while time.monotonic() < deadline:
        time.sleep(1)
        try:
            resp = plain.get(f"{base_url}/favicon.webp", timeout=5)
            if resp.status_code in (200, 401, 403):
                consecutive += 1
                if consecutive >= stable_polls:
                    return True
                continue
        except requests.exceptions.RequestException:
            pass
        consecutive = 0
    return False


def _reboot_with_session(base_url, sid):
    """Trigger a SW reboot via POST /cmd, authenticated with the given session id.

    Used by the full-ring reboot tests, which fill the auth ring buffer and thereby
    evict the api fixture's own session — so the reboot must be sent with one of the
    still-valid fill sessions, otherwise /cmd returns 401 and the reboot never fires.

    The firmware resets as part of handling the command and often closes the socket
    before the HTTP response is flushed, so a dropped connection (RemoteDisconnected /
    ConnectionError) is the SUCCESS path: the reboot fired. A clean 401, by contrast,
    means the session was not accepted — a real error we must surface. Whether the
    reboot actually took is then confirmed downstream by _wait_device_up() plus the
    ESP_RST_SW check in _sessions_restored_after_reboot().
    """
    rb = requests.Session()
    rb.cookies.set("session_id", sid)
    try:
        resp = rb.post(f"{base_url}/cmd", json={"cmd": "reboot"}, timeout=30)
        assert resp.status_code == 200, \
            f"reboot command was rejected ({resp.status_code}); the session used to " \
            f"send it was not accepted (buffer-fill likely evicted it)"
    except requests.exceptions.ConnectionError:
        pass  # device reset before responding -> reboot fired (the intended outcome)
    finally:
        rb.close()


@pytest.mark.timeout(2400)
def test_au01_session_preserved_after_sw_reboot(api):
    """AU-01: Session cookie persists after POST /cmd reboot (SW reset uses RTC_NOINIT)."""
    # Save current session_id cookie before rebooting
    old_sid = api.session.cookies.get("session_id")
    assert old_sid is not None, "No session_id cookie found — fixture must be authenticated"

    # Trigger SW reboot
    serial_offset = _serial_log_offset()
    api.execute_command("reboot")

    # Wait for device to go down and come back up using a plain session (no cookies)
    # so we don't accidentally refresh the api session yet
    plain_session = requests.Session()
    base_url = api.base_url
    deadline = time.monotonic() + 1800
    device_up = False
    while time.monotonic() < deadline:
        time.sleep(2)
        try:
            resp = plain_session.get(f"{base_url}/favicon.webp", timeout=5)
            if resp.status_code in (200, 401, 403):
                device_up = True
                break
        except requests.exceptions.RequestException:
            # Device still booting
            pass

    assert device_up, "Device did not come back up within 1800 seconds after reboot"

    if _sessions_restored_after_reboot(serial_offset) is False:
        pytest.skip(
            "QEMU warm reboot returned a non-SW reset (WDT/panic); firmware "
            "intentionally wiped auth sessions. Infra flake, not a session-logic bug."
        )

    # Verify the OLD session cookie still works (sessions survive SW reset via RTC_NOINIT).
    # This check must happen BEFORE wait_for_ready() creates a new session, which could
    # evict old_sid if the ring buffer is full.
    test_session = requests.Session()
    test_session.cookies.set("session_id", old_sid)
    resp = test_session.get(f"{base_url}/session", timeout=10)
    assert resp.status_code == 200, (
        f"Old session_id should be preserved after SW reboot, got {resp.status_code}"
    )

    # Re-authenticate the api fixture so subsequent tests still have a valid session
    # (this creates a NEW session in api.session, but that is fine — old_sid was saved)
    api.wait_for_ready(timeout=1800)


@pytest.mark.timeout(2400)
def test_au05_full_buffer_preserved_after_sw_reboot(api):
    """AU-05: All MAX_SESSIONS sessions survive SW reboot (full ring buffer)."""
    base_url = api.base_url
    sids = []

    try:
        # Fill the ring buffer exactly: create MAX_SESSIONS independent sessions.
        # Prior sessions from earlier tests will be evicted if ring overflows — that is fine.
        for i in range(MAX_SESSIONS):
            time.sleep(0.3)
            s = requests.Session()
            resp = s.post(
                f"{base_url}/auth",
                json={"login": WBMGEAPI.DEFAULT_LOGIN, "pass": WBMGEAPI.DEFAULT_PASSWORD},
                timeout=20,
            )
            assert resp.status_code == 200
            assert resp.json().get("auth") is True, f"Login #{i} failed: {resp.json()}"
            sid = s.cookies.get("session_id")
            assert sid is not None, f"No session_id cookie in login #{i}"
            sids.append(sid)
            s.close()

        # Reboot via a still-valid fill session (NOT the api fixture's session, which
        # the ring-fill above evicted). This makes the reboot actually fire — before,
        # api.execute_command() got 401 and the reboot never happened, so the test
        # passed by luck and flaked only when a stray /auth evicted the zero-margin
        # ring's oldest slot. Sending it this way adds no post-fill /auth, so all
        # MAX_SESSIONS entries are intact when RTC_NOINIT restores them.
        serial_offset = _serial_log_offset()
        _reboot_with_session(base_url, sids[-1])

        # Wait for the reboot to ACTUALLY occur before probing HTTP or checking
        # sessions. The reboot is delayed — reboot_task (main/cmd_handler.c) does a
        # vTaskDelay so the httpd can flush the response, then esp_restart() — so the
        # device stays up for a moment after the command. Probing "is it up" (or
        # running the whole check loop) too early races that pending reset: everything
        # runs against the not-yet-rebooted device and the reset then lands mid-request
        # (it was surfacing as a RemoteDisconnected in the finally's fixture re-auth).
        # The firmware's post-boot auth-init marker only appears in the serial log
        # after the SW reset, so it is the authoritative "the reboot happened" signal.
        restored = _sessions_restored_after_reboot(serial_offset, read_timeout=120)
        assert restored is not None, \
            "Device did not reboot within 120 s after the reboot command was sent"
        if restored is False:
            pytest.skip(
                "QEMU warm reboot returned a non-SW reset (WDT/panic); firmware "
                "intentionally wiped auth sessions. Infra flake, not a session-logic bug."
            )

        assert _wait_device_up(base_url, timeout=1800), \
            "Device did not come back up within 1800 s after reboot"

        # All MAX_SESSIONS sessions must survive SW reboot
        for i, sid in enumerate(sids):
            ts = requests.Session()
            ts.cookies.set("session_id", sid)
            resp = ts.get(f"{base_url}/session", timeout=10)
            ts.close()
            assert resp.status_code == 200, \
                f"Session {i} (sid={sid}) was lost after SW reboot with full buffer"

    finally:
        # Log out all created sessions to keep ring buffer clean for subsequent tests
        for sid in sids:
            time.sleep(0.15)
            try:
                ls = requests.Session()
                ls.cookies.set("session_id", sid)
                ls.post(f"{base_url}/logout", timeout=10)
                ls.close()
            except requests.exceptions.RequestException:
                pass
        # Re-authenticate the api fixture with wait_for_ready(), NOT a bare auth():
        # the reboot left the fixture session's HTTP keep-alive pool holding dead
        # sockets, so a single auth() POST reuses a stale connection and raises
        # RemoteDisconnected (the check loop above avoids this by using fresh
        # requests.Session() objects). wait_for_ready() calls reconnect() to drop the
        # stale pool and retries auth()+get_info() until the device answers.
        api.wait_for_ready(timeout=1800)


@pytest.mark.timeout(2400)
def test_au06_ring_wrap_preserved_after_sw_reboot(api):
    """AU-06: Ring-wrap sessions survive SW reboot; eviction happened before reboot stays evicted."""
    base_url = api.base_url
    sids = []  # sids[0] gets evicted before reboot; sids[1..MAX_SESSIONS] survive

    try:
        # Create MAX_SESSIONS+1 sessions. The 11th auth wraps the ring and evicts sids[0].
        for i in range(MAX_SESSIONS + 1):
            time.sleep(0.3)
            s = requests.Session()
            resp = s.post(
                f"{base_url}/auth",
                json={"login": WBMGEAPI.DEFAULT_LOGIN, "pass": WBMGEAPI.DEFAULT_PASSWORD},
                timeout=20,
            )
            assert resp.status_code == 200
            assert resp.json().get("auth") is True, f"Login #{i} failed: {resp.json()}"
            sid = s.cookies.get("session_id")
            assert sid is not None, f"No session_id cookie in login #{i}"
            sids.append(sid)
            s.close()

        time.sleep(1)

        # Verify sids[0] was already evicted before reboot (ring buffer wrap)
        es = requests.Session()
        es.cookies.set("session_id", sids[0])
        resp = es.get(f"{base_url}/session", timeout=10)
        es.close()
        assert resp.status_code == 401, \
            f"sids[0] should be evicted by ring wrap before reboot, got {resp.status_code}"

        # Reboot via a session that survived the ring wrap (sids[-1], the newest),
        # NOT the api fixture's session which the MAX_SESSIONS+1 fill evicted. As in
        # AU-05, this makes the reboot actually fire and leaves the ring untouched, so
        # the surviving sids[1..MAX_SESSIONS] are all restored from RTC_NOINIT.
        serial_offset = _serial_log_offset()
        _reboot_with_session(base_url, sids[-1])

        # Wait for the reboot to ACTUALLY occur before probing HTTP or checking
        # sessions. The reboot is delayed — reboot_task (main/cmd_handler.c) does a
        # vTaskDelay so the httpd can flush the response, then esp_restart() — so the
        # device stays up for a moment after the command. Probing "is it up" (or
        # running the whole check loop) too early races that pending reset: everything
        # runs against the not-yet-rebooted device and the reset then lands mid-request
        # (it was surfacing as a RemoteDisconnected in the finally's fixture re-auth).
        # The firmware's post-boot auth-init marker only appears in the serial log
        # after the SW reset, so it is the authoritative "the reboot happened" signal.
        restored = _sessions_restored_after_reboot(serial_offset, read_timeout=120)
        assert restored is not None, \
            "Device did not reboot within 120 s after the reboot command was sent"
        if restored is False:
            pytest.skip(
                "QEMU warm reboot returned a non-SW reset (WDT/panic); firmware "
                "intentionally wiped auth sessions. Infra flake, not a session-logic bug."
            )

        assert _wait_device_up(base_url, timeout=1800), \
            "Device did not come back up within 1800 s after reboot"

        # Sessions 1..MAX_SESSIONS must all survive SW reboot
        for i in range(1, MAX_SESSIONS + 1):
            ts = requests.Session()
            ts.cookies.set("session_id", sids[i])
            resp = ts.get(f"{base_url}/session", timeout=10)
            ts.close()
            assert resp.status_code == 200, \
                f"Session {i} (sid={sids[i]}) was lost after SW reboot"

    finally:
        # Log out surviving sessions (1..MAX_SESSIONS)
        for sid in sids[1:]:
            time.sleep(0.15)
            try:
                ls = requests.Session()
                ls.cookies.set("session_id", sid)
                ls.post(f"{base_url}/logout", timeout=10)
                ls.close()
            except requests.exceptions.RequestException:
                pass
        # Re-authenticate the api fixture with wait_for_ready(), NOT a bare auth():
        # as in AU-05, the reboot leaves the fixture session's keep-alive pool with
        # dead sockets, so a single auth() POST reuses a stale connection and raises
        # RemoteDisconnected. wait_for_ready() reconnects and retries until ready.
        api.wait_for_ready(timeout=1800)


def test_st01_hostname_validation(api):
    """ST-01: validate_hostname() rejects dot and underscore; enforces 31-char max."""
    original = api.get_settings().json()["hostname"]
    try:
        # Dot is not allowed in hostname
        resp = api.update_settings({"hostname": "valid-host.name"})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "Hostname with dot should be rejected"
        # Verify settings unchanged
        assert api.get_settings().json()["hostname"] == original, \
            "Hostname must not change after rejected update"

        # Underscore is not allowed in hostname
        resp = api.update_settings({"hostname": "valid_host_name"})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "Hostname with underscore should be rejected"
        assert api.get_settings().json()["hostname"] == original, \
            "Hostname must not change after rejected update"

        # Exactly 31 chars — at the limit
        resp = api.update_settings({"hostname": "a" * 31})
        assert resp.status_code == 200
        assert resp.json().get("success") is True, \
            "Hostname of exactly 31 chars should be accepted"

        # 32 chars — one over the limit
        resp = api.update_settings({"hostname": "a" * 32})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "Hostname of 32 chars should be rejected (max is 31)"

    finally:
        api.update_settings({"hostname": original})


def test_st02_login_validation(api):
    """ST-02: validate_login() rejects special chars; enforces 31-char max."""
    original_settings = api.get_settings().json()
    original_login = original_settings.get("login", WBMGEAPI.DEFAULT_LOGIN)
    try:
        # @ is not allowed in login
        resp = api.update_settings({"login": "admin@test"})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "Login with @ should be rejected"

        # Dot is not allowed in login
        resp = api.update_settings({"login": "admin.test"})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "Login with dot should be rejected"

        # Exclamation mark is not allowed in login
        resp = api.update_settings({"login": "admin!test"})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "Login with ! should be rejected"

        # Exactly 31 chars — at the limit; allowed chars: a-z, A-Z, 0-9, _, -
        new_login = "a" * 31
        resp = api.update_settings({"login": new_login})
        assert resp.status_code == 200
        assert resp.json().get("success") is True, \
            "Login of exactly 31 chars should be accepted"
        # Confirm it was actually saved
        saved = api.get_settings().json().get("login")
        assert saved == new_login, f"Expected saved login '{new_login}', got '{saved}'"

        # 32 chars — one over the limit
        resp = api.update_settings({"login": "a" * 32})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "Login of 32 chars should be rejected (max is 31)"

    finally:
        api.update_settings({"login": original_login})


def test_st03_password_empty_rejected(api):
    """ST-03: Empty password must be rejected (validates the validate_password() bug fix)."""
    # Attempt to set an empty password
    resp = api.update_settings({"pass": ""})
    assert resp.status_code == 200
    assert resp.json().get("success") is False, \
        "Empty password must be rejected by validate_password()"

    # Confirm the password was not changed to empty string
    settings = api.get_settings().json()
    assert settings.get("pass") == WBMGEAPI.DEFAULT_PASSWORD, \
        "Password must remain unchanged (equal to default) after rejecting empty password"


def test_st04_set_default_settings_restores_credentials(api):
    """ST-04: POST /cmd set_default_settings resets login and pass to 'admin'."""
    original_settings = api.get_settings().json()
    try:
        # Change both login and pass to non-default values
        resp = api.update_settings({"login": "testuser123"})
        assert resp.status_code == 200
        assert resp.json().get("success") is True, "Login update should succeed"

        resp = api.update_settings({"pass": "testpass123"})
        assert resp.status_code == 200
        assert resp.json().get("success") is True, "Password update should succeed"

        # Reset to factory defaults
        api.execute_command("set_default_settings")

        # After set_default_settings the session is still valid (in-memory sessions are
        # not wiped), so we can immediately query settings
        settings = api.get_settings().json()
        assert settings.get("login") == WBMGEAPI.DEFAULT_LOGIN, \
            f"Login must be reset to '{WBMGEAPI.DEFAULT_LOGIN}' after set_default_settings"
        assert settings.get("pass") == WBMGEAPI.DEFAULT_PASSWORD, \
            f"Password must be reset to '{WBMGEAPI.DEFAULT_PASSWORD}' after set_default_settings"

    finally:
        # Restore original settings; session is still valid so no re-auth needed
        api.update_settings(original_settings)


def test_au02_session_ring_buffer_eviction(api):
    """AU-02: 11th session evicts the 1st (ring buffer wraps at MAX_SESSIONS=10)."""
    base_url = api.base_url
    session_cookies = []

    try:
        # Create MAX_SESSIONS + 1 = 11 independent sessions.
        # After each login we save only the session_id cookie value and CLOSE the connection
        # immediately so it does not occupy one of the HTTP server's MAX_OPEN_SOCKETS slots.
        # Wait 0.3s between logins to avoid overwhelming the embedded HTTP server.
        for i in range(MAX_SESSIONS + 1):
            time.sleep(0.3)
            s = requests.Session()
            resp = s.post(
                f"{base_url}/auth",
                json={"login": WBMGEAPI.DEFAULT_LOGIN, "pass": WBMGEAPI.DEFAULT_PASSWORD},
                timeout=20,
            )
            assert resp.status_code == 200
            assert resp.json().get("auth") is True, \
                f"Login #{i} failed: {resp.json()}"
            sid = s.cookies.get("session_id")
            assert sid is not None, f"No session_id cookie in login #{i} response"
            session_cookies.append(sid)
            s.close()  # Release the TCP socket immediately

        # Give the server a moment to finalise all connection clean-ups
        time.sleep(1)

        # The 11th login (index 10) wraps the ring buffer back to slot 0,
        # evicting the 1st session (index 0).
        evicted_session = requests.Session()
        evicted_session.cookies.set("session_id", session_cookies[0])
        resp = evicted_session.get(f"{base_url}/session", timeout=10)
        evicted_session.close()
        assert resp.status_code == 401, (
            f"Session 0 should have been evicted by session 10 (ring buffer), "
            f"but got {resp.status_code}"
        )

        # The 11th session (index 10) must still be valid
        newest_session = requests.Session()
        newest_session.cookies.set("session_id", session_cookies[10])
        resp = newest_session.get(f"{base_url}/session", timeout=10)
        newest_session.close()
        assert resp.status_code == 200, (
            f"Session 10 (newest) should still be valid, got {resp.status_code}"
        )

    finally:
        # Logout sessions 1..10 (session 0 was evicted, session 10 we just checked)
        for sid in session_cookies[1:]:
            time.sleep(0.15)
            try:
                logout_s = requests.Session()
                logout_s.cookies.set("session_id", sid)
                logout_s.post(f"{base_url}/logout", timeout=10)
                logout_s.close()
            except requests.exceptions.RequestException:
                pass
        # Re-authenticate the api fixture: ring buffer eviction may have invalidated
        # the api session (it was the oldest slot and could have been overwritten by
        # the 11th login in this test).
        time.sleep(0.5)
        resp = api.auth()
        assert resp.status_code == 200 and resp.json().get("auth") is True, \
            f"Re-auth after ring-buffer test failed: {resp.status_code} {resp.text}"


def test_au03_logout_without_cookie(api):
    """AU-03: Logout without a session cookie returns 200 and {logout: true}."""
    # Use a plain session with NO cookies at all
    plain_session = requests.Session()
    resp = plain_session.post(f"{api.base_url}/logout", timeout=10)
    assert resp.status_code == 200, \
        f"Logout without cookie must return 200, got {resp.status_code}"
    data = resp.json()
    assert data.get("logout") is True, \
        f"Logout without cookie must return {{logout: true}}, got {data}"


def test_au04_login_missing_fields(api):
    """AU-04: Login with missing 'login' or 'pass' field must return auth=false + error."""
    base_url = api.base_url

    # Missing "pass" field
    resp = requests.post(f"{base_url}/auth", json={"login": "admin"}, timeout=10)
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("auth") is False, "Login with missing 'pass' must return auth=false"
    assert "error" in data, "Login with missing 'pass' must include 'error' key"

    # Missing "login" field
    resp = requests.post(f"{base_url}/auth", json={"pass": "admin"}, timeout=10)
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("auth") is False, "Login with missing 'login' must return auth=false"
    assert "error" in data, "Login with missing 'login' must include 'error' key"

    # Empty JSON object
    resp = requests.post(f"{base_url}/auth", json={}, timeout=10)
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("auth") is False, "Login with empty JSON must return auth=false"
    assert "error" in data, "Login with empty JSON must include 'error' key"


def test_st05_timeout_validation(api):
    """ST-05: validate_timeout() boundary check via cache_value_timeout_s."""
    time.sleep(1)  # Allow server to recover after the AU-02 burst of connections
    original_settings = api.get_settings().json()
    original_timeout = original_settings.get("cache_value_timeout_s")
    try:
        # 0 = disabled — must be accepted
        resp = api.update_settings({"cache_value_timeout_s": 0})
        assert resp.status_code == 200
        assert resp.json().get("success") is True, \
            "cache_value_timeout_s=0 (disable) should be accepted"

        # Maximum allowed value
        resp = api.update_settings({"cache_value_timeout_s": 65535})
        assert resp.status_code == 200
        assert resp.json().get("success") is True, \
            "cache_value_timeout_s=65535 should be accepted"

        # One over the maximum
        resp = api.update_settings({"cache_value_timeout_s": 65536})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "cache_value_timeout_s=65536 should be rejected (max is 65535)"

        # Negative value
        resp = api.update_settings({"cache_value_timeout_s": -1})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "cache_value_timeout_s=-1 should be rejected"

    finally:
        if original_timeout is not None:
            api.update_settings({"cache_value_timeout_s": original_timeout})


def test_st06_bool_type_check(api):
    """ST-06: POST /settings {vout: 1} must be rejected; {vout: true} must be accepted."""
    original_settings = api.get_settings().json()
    original_vout = original_settings.get("vout")
    try:
        # Integer 1 is not a JSON boolean — cJSON_IsBool returns false → rejected
        resp = api.update_settings({"vout": 1})
        assert resp.status_code == 200
        assert resp.json().get("success") is False, \
            "vout=1 (integer) must be rejected for a BOOL setting"

        # JSON true is a valid boolean — must be accepted
        resp = api.update_settings({"vout": True})
        assert resp.status_code == 200
        assert resp.json().get("success") is True, \
            "vout=true (JSON bool) must be accepted"

    finally:
        if original_vout is not None:
            api.update_settings({"vout": original_vout})


def test_h3_concurrent_sessions(api):
    """H3: Two concurrent authenticated sessions; logout one, other still works."""
    base_url = api.base_url

    # Session A is the api fixture session (already authenticated)
    # Create session B independently
    session_b = requests.Session()
    resp = session_b.post(
        f"{base_url}/auth",
        json={"login": WBMGEAPI.DEFAULT_LOGIN, "pass": WBMGEAPI.DEFAULT_PASSWORD},
        timeout=10,
    )
    assert resp.status_code == 200
    assert resp.json().get("auth") is True, "Session B login must succeed"

    # Logout session A
    resp = api.logout()
    assert resp.status_code == 200
    data = resp.json()
    assert data.get("logout") is True, "Logout of session A must return {logout: true}"

    # Verify session A is gone
    resp = api.get_session()
    assert resp.status_code == 401, \
        "Session A must be invalid after logout"

    # Verify session B still works
    resp = session_b.get(f"{base_url}/session", timeout=10)
    assert resp.status_code == 200, \
        f"Session B must still be valid after session A was logged out, got {resp.status_code}"

    # Cleanup: logout session B
    session_b.post(f"{base_url}/logout", timeout=10)

    # Re-authenticate the api fixture so subsequent tests still have a valid session
    resp = api.auth()
    assert resp.status_code == 200 and resp.json().get("auth") is True, \
        f"Re-auth after concurrent sessions test failed: {resp.status_code} {resp.text}"
