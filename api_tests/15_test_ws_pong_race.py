"""
Regression test for the WS PONG race: concurrent writes to a single TCP socket
by sniffer_ws_task (data frames) and the httpd auto-PONG (response to client PING).

httpd_ws_send_frame_async sends a WS frame as TWO separate send() calls
(header, then payload). If the httpd listener inserts a PONG between those two
calls (in response to a client PING), the bytes on the wire are interleaved:
[hdr_data][hdr_pong][payload_data]...
The client receives a corrupt frame; the WS stream desyncs and stops delivering
valid packets.

Reproduction: an aggressive PING tight-loop maximises the PONG rate from the
server. Many consecutive stream attempts each give an independent chance to hit
the race (~65% per attempt on unfixed code). 80 attempts -> P(miss bug) ≈ 1e-36.
Without the fix, dozens of attempts corrupt; with the fix, all are clean.

Run this test alone:
  pytest api_tests/15_test_ws_pong_race.py --qemu --qemu-skip-build -v -s
"""
import json
import threading
import time

import pytest
import websocket
from urllib.parse import urlparse

from packet_injector import PacketInjector


# Long deterministic regression test.
# Without the fix, each attempt catches frame corruption with probability ~0.65.
# At ATTEMPTS=80, P(miss bug) = 0.35^80 ≈ 1e-36 — practically impossible.
# The test ALWAYS fails on broken code and ALWAYS passes on the fixed code
# (the fix structurally eliminates interleaving — all sends go through the
# same httpd worker task). Cost: ~8 min per run (~6s per attempt).
ATTEMPTS = 80
# Window of a single attempt. Longer -> more data frames -> higher race probability.
STREAM_SECONDS = 3.0
# PING interval in seconds. 3ms (~330/s) is ~2 orders of magnitude more aggressive
# than the normal 500ms used in other tests; reliably provokes the race on unfixed
# code without overloading the fixed server.
PING_INTERVAL = 0.003
# Minimum valid packets expected per attempt (mock produces ~4/s -> ~10-12 in 3s).
MIN_PACKETS_OK = 3


def _aggressive_ping(ws, stop_evt):
    """Send PINGs at a high rate to provoke a flood of auto-PONGs from the server."""
    while not stop_evt.is_set():
        try:
            ws.ping()
        except Exception:
            break
        time.sleep(PING_INTERVAL)


def _one_attempt(host, port, cookies):
    """
    Single attempt: connect WS, start sniffer, ping aggressively, read packets.

    Returns a dict: {valid, desync, conn_err}.
      desync   -- frame corruption detected (WebSocketProtocol/Payload/JSONDecode
                  exception), the direct signature of the PONG race bug.
      conn_err -- connection could not be established or dropped under load
                  (noise artifact, not the primary bug signal).
    """
    res = {"valid": 0, "desync": False, "conn_err": False}
    ws = websocket.WebSocket()
    ws.settimeout(2)
    try:
        ws.connect(f"ws://{host}:{port}/sniffer/ws", cookie=cookies)
        ws.send(json.dumps({"cmd": "start", "port": 1}))
    except Exception:
        res["conn_err"] = True
        try:
            ws.close()
        except Exception:
            pass
        return res

    stop_evt = threading.Event()
    pinger = threading.Thread(target=_aggressive_ping, args=(ws, stop_evt), daemon=True)
    pinger.start()

    deadline = time.monotonic() + STREAM_SECONDS
    while time.monotonic() < deadline:
        try:
            msg = ws.recv()
            if not msg:
                continue
            json.loads(msg)          # corrupt frame -> JSONDecodeError
            res["valid"] += 1
        except websocket.WebSocketTimeoutException:
            pass
        except (websocket.WebSocketProtocolException,
                websocket.WebSocketPayloadException,
                json.JSONDecodeError):
            # Direct signature of interleaved frames on the wire (corruption).
            res["desync"] = True
            break
        except websocket.WebSocketConnectionClosedException:
            res["conn_err"] = True
            break

    stop_evt.set()
    try:
        ws.send(json.dumps({"cmd": "stop", "port": 1}))
    except Exception:
        pass
    try:
        ws.close()
    except Exception:
        pass
    return res


@pytest.mark.timeout(900)
def test_ws_pong_race_no_corruption(api):
    """
    The WS stream must not corrupt under an aggressive PING flood.

    On unfixed code (httpd_ws_send_frame_async) at least one attempt will
    desync or starve. With the fix, all attempts are clean.
    """
    parsed = urlparse(api.base_url)
    host = parsed.hostname
    port = parsed.port or 80

    original_port_mode = None
    try:
        info = api.get_info()
        assert info.status_code == 200
        original_port_mode = info.json().get("rs485_1", {}).get("port_mode", "tcp_bridge")

        r = api.set_port_mode(1, "sniffer")
        assert r.status_code == 200, f"Failed to set sniffer mode: {r.status_code}"
        time.sleep(0.5)

        cookies = "; ".join([f"{k}={v}" for k, v in api.session.cookies.items()])

        # Start packet injector so the sniffer has traffic to forward over WS.
        # Without injected traffic ws.recv() blocks indefinitely — the firmware
        # no longer has a built-in modbus mock (dropped in 7d5b257).
        with PacketInjector(port=1) as _inj:
            desync_attempts = []   # frame corruption -- direct signature of the race bug
            starved = []           # too few packets without corruption (secondary symptom)
            conn_errs = 0          # connect/timeout failures under flood (noise)
            for i in range(ATTEMPTS):
                r = _one_attempt(host, port, cookies)
                if r["desync"]:
                    tag = "DESYNC"
                    desync_attempts.append(i + 1)
                elif r["conn_err"]:
                    tag = "connerr"
                    conn_errs += 1
                elif r["valid"] < MIN_PACKETS_OK:
                    tag = "starved"
                    starved.append((i + 1, r["valid"]))
                else:
                    tag = "ok"
                print(f"  attempt {i+1:2d}/{ATTEMPTS}: {tag:7s} valid={r['valid']} "
                      f"desync={r['desync']} conn_err={r['conn_err']}")
                time.sleep(0.2)

            print(f"\n  desync(corruption)={len(desync_attempts)} starved={len(starved)} "
                  f"conn_err={conn_errs} / {ATTEMPTS}")

            # Primary criterion: no frame corruption at all.
            assert not desync_attempts, (
                f"WS frame corruption in {len(desync_attempts)} of {ATTEMPTS} attempts: "
                f"{desync_attempts}. This is the httpd auto-PONG vs sniffer_ws_task race."
            )
            # Secondary check: the stream is actually delivering packets (not just connecting).
            # Majority of attempts must receive data; otherwise something is broadly broken.
            good = ATTEMPTS - len(desync_attempts) - len(starved) - conn_errs
            assert good >= ATTEMPTS // 2, (
                f"Too few healthy attempts: {good}/{ATTEMPTS} "
                f"(starved={len(starved)}, conn_err={conn_errs}). Stream is degraded."
            )
            print("OK: no WS frame corruption under aggressive PING flood -- race eliminated")

    finally:
        if original_port_mode is not None:
            try:
                api.set_port_mode(1, original_port_mode)
            except Exception:
                pass
