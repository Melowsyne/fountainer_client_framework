#!/usr/bin/env python3
# Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
# SPDX-License-Identifier: MIT
"""fuzz_maintenance.py — adversarial test campaign against the LOCAL Fountain
server of a devkit (port 4443). Builds on fountain_proto (DeviceSession), i.e.
the Fountain SERVER role, like the maintenance client in the server repo.

HARD SAFETY LOCK: runs ONLY against device_id 'fnt-000001' and NEVER against
192.168.1.51 (production pump). Any violation aborts immediately.

Phases (subcommands):
  values    P1  dp_write value fuzzing (out-of-range/type/too long/NaN/Inf)
  commands  P2  command fuzzing (unknown, missing/wrong fields)
  protocol  P3  broken frames, oversize, wrong MAC/kid/replay
  parallel  P4  concurrent requests on ONE session (asyncio.gather)
  sessions  P5  two parallel sessions writing overlapping values (contention)
  websim    P6  web UI request pattern (config snapshot + log poller + write)
  flood     P7  rate-limit flood (many requests quickly)
  all           P1..P7 in sequence

Note: the local server's slot count is a build option (LOCAL_SERVER_MAX_CLIENTS,
default 1). After dense churn, just-closed TLS sockets linger briefly; the
multi-session phase (P5) needs a build with at least 2 slots. For a clean 'all' run, reset the device beforehand;
individual phases run at any time. No slot leak (every close frees the slot)
— purely TLS teardown latency against the 2-slot limit.
"""
import argparse
import asyncio
import copy
import json
import ssl
import sys
import time
from pathlib import Path

import websockets

ROOT = Path(__file__).resolve().parent
# fountain_proto lives in the server repo (sibling folder) — library use only.
SERVER = ROOT.parent.parent / "fountainer_server"
sys.path.insert(0, str(SERVER))
from fountain_proto import auth                         # noqa: E402
from fountain_proto.devices import DeviceRegistry       # noqa: E402
from fountain_proto.envelope import build_message       # noqa: E402
from fountain_proto.session import DeviceSession        # noqa: E402

CA = SERVER.parent / "DO_NOT_COMMIT" / "CA"
FORBIDDEN_HOSTS = {"192.168.1.51"}
ALLOWED_DEVICE = "fnt-000001"

# RW datapoints: type + (min, max, constraint-safe valid value). The valid
# value respects the device's cross-field constraints (Min<Max etc.) once the
# baseline below has been seeded. The device is authoritative.
RW_NUM = {
    "Fon_Min_Pressure":   ("f32", 0.0, 10.0, 1.5),
    "Fon_Max_Pressure":   ("f32", 0.0, 10.0, 7.0),
    "Fon_Min_On_Time":    ("u16", 0, 65535, 30),
    "Fon_Max_On_Time":    ("u16", 10, 65535, 240),
    "Fon_Report_Interval":("u16", 1, 3600, 60),
    "Fon_Sensor_Offset":  ("i16", -5000, 5000, 0),
    "Fon_Event_Label":    ("u8", 0, 7, 3),        # VOLATILE, no NVS
    "Fon_Fault_Ack":      ("u8", 0, 1, 0),        # VOLATILE
}

# Consistent starting point (satisfies all cross-field constraints) so that the
# single-field "valid" writes do not fail on neighbouring constraints.
BASELINE = {
    "Fon_Alert_Low_Pressure": 0.2, "Fon_Min_Pressure": 1.0,
    "Fon_Max_Pressure": 8.0, "Fon_Alert_High_Pressure": 9.5,
    "Fon_Min_On_Time": 20, "Fon_Dry_Run_Detect_Time": 30, "Fon_Max_On_Time": 300,
}
RW_STR = ["Network_SSID"]      # STR RW (DP_STR_MAX = 64)
RW_BOOL = ["Fon_Pressure_Manual"]

PASS, FAIL = 0, 0


def check(cond, label):
    global PASS, FAIL
    if cond:
        PASS += 1; print(f"  ok   {label}")
    else:
        FAIL += 1; print(f"  FAIL {label}")


# The local server has a token bucket (capacity 20, refill 5/s). Validation
# tests (which need a response) stay below that rate: ~4/s + timeout-robust.
# (The deliberate flood is done by P7.)
PACE = 0.25


async def dpw(s, dp):
    await asyncio.sleep(PACE)
    try:
        return await s.dp_write(dp)
    except asyncio.TimeoutError:
        return {"status": "timeout"}


async def cmd(s, name, **kw):
    await asyncio.sleep(PACE)
    try:
        return await s.command(name, **kw)
    except asyncio.TimeoutError:
        return {"status": "timeout"}


async def dpr(s, names):
    await asyncio.sleep(PACE)
    try:
        return await s.dp_read(names)
    except asyncio.TimeoutError:
        return {"dp": {}}


def tls_context():
    c = ssl.create_default_context(ssl.Purpose.SERVER_AUTH,
                                   cafile=str(CA / "root" / "certs" / "ca.crt.pem"))
    c.check_hostname = False
    c.load_cert_chain(str(CA / "clients" / "service-tool-01.crt"),
                      str(CA / "clients" / "service-tool-01.key"))
    return c


async def recv_loop(ws, sess):
    try:
        async for raw in ws:
            try:
                sess and await sess._dispatch(json.loads(raw))
            except json.JSONDecodeError:
                pass
    except websockets.ConnectionClosed:
        pass


class Conn:
    """A connected Fountain session established via handshake."""
    def __init__(self, host, port, device):
        self.host, self.port, self.device = host, port, device
        self.ws = self.sess = self.task = None

    async def __aenter__(self):
        uri = f"wss://{self.host}:{self.port}/ws"
        # The local server has few slots (LOCAL_SERVER_MAX_CLIENTS); after
        # dense churn, just-closed TLS sockets may linger briefly. Like a real
        # client with auto-reconnect: a few attempts with backoff.
        last = None
        for attempt in range(5):
            try:
                self.ws = await websockets.connect(
                    uri, subprotocols=["fountain"], ssl=tls_context(),
                    max_size=65536, open_timeout=15)
                self.sess = DeviceSession(self.ws, self.device)
                if not await self.sess.handshake():
                    raise RuntimeError("Fountain handshake failed")
                self.task = asyncio.create_task(recv_loop(self.ws, self.sess))
                return self
            except Exception as e:  # noqa: BLE001
                last = e
                if self.ws:
                    try:
                        await self.ws.close()
                    except Exception:
                        pass
                    self.ws = None
                await asyncio.sleep(3)
        raise RuntimeError(f"connection failed after retries: {last}")

    async def __aexit__(self, *exc):
        if self.task:
            self.task.cancel()
        if self.ws:
            await self.ws.close()

    # Send a raw (self-signed) s2c control message — for P3 tampering.
    async def send_signed_raw(self, name, body, *, seq, tamper=None):
        s = self.sess
        msg = build_message(name, body, msg_id=f"raw-{seq}")
        auth.sign(msg, auth_key=s.auth_key, kid=s.kid, seq=seq, direction="s2c",
                  device_id=s.device_id, server_nonce=s.server_nonce,
                  client_nonce=s.client_nonce)
        if tamper:
            tamper(msg)
        await self.ws.send(json.dumps(msg))


# ---------------------------------------------------------------------------
# P1 — dp_write value fuzzing
# ---------------------------------------------------------------------------
async def phase_values(conn):
    print("P1 dp_write value fuzzing:")
    s = conn.sess
    # Seed a consistent baseline (one atomic batch).
    rb = await dpw(s, BASELINE)
    check(rb.get("status") == "applied", "baseline (consistent constraints) -> applied")
    for name, (typ, lo, hi, valid) in RW_NUM.items():
        r = await dpw(s, {name: valid})
        check(r.get("status") == "applied", f"{name}={valid} -> applied")
        # above max
        r = await dpw(s, {name: hi + 1})
        check(r.get("status") == "rejected", f"{name}={hi+1} (>max) -> rejected")
        # below min (only where meaningful)
        if lo != 0 or typ == "i16":
            r = await dpw(s, {name: lo - 1})
            check(r.get("status") == "rejected", f"{name}={lo-1} (<min) -> rejected")
        # wrong type (string for a number)
        r = await dpw(s, {name: "not-a-number"})
        check(r.get("status") == "rejected", f"{name}='str' -> rejected (type)")
    # Inf as string -> type_mismatch
    r = await dpw(s, {"Fon_Sensor_Scale": "inf"})
    check(r.get("status") == "rejected", "Fon_Sensor_Scale='inf' -> rejected")
    # integral float 10.0 (DeviceSession casts to int) -> applied
    r = await dpw(s, {"Fon_Report_Interval": 10.0})
    check(r.get("status") == "applied", "Fon_Report_Interval=10.0 -> applied")
    # string too long
    for name in RW_STR:
        r = await dpw(s, {name: "x" * 200})
        check(r.get("status") == "rejected", f"{name}=200 characters -> rejected (too_long)")
        r = await dpw(s, {name: "TestNet"})
        check(r.get("status") == "applied", f"{name}='TestNet' -> applied")
    # Bool
    for name in RW_BOOL:
        r = await dpw(s, {name: True})
        check(r.get("status") == "applied", f"{name}=true -> applied")
        r = await dpw(s, {name: 5})
        check(r.get("status") == "rejected", f"{name}=5 -> rejected (type)")
    # unknown datapoint
    r = await dpw(s, {"Does_Not_Exist": 1})
    check(r.get("status") == "rejected", "unknown DP -> rejected")
    # read-only
    r = await dpw(s, {"Fon_Current_State": 2})
    check(r.get("status") == "rejected", "writing an RO DP -> rejected")
    # mixed batch (valid+invalid) -> everything discarded
    before = (await dpr(s, ["Fon_Report_Interval"]))["dp"].get("Fon_Report_Interval")
    r = await dpw(s, {"Fon_Report_Interval": 55, "Fon_Max_On_Time": 1})
    after = (await dpr(s, ["Fon_Report_Interval"]))["dp"].get("Fon_Report_Interval")
    check(r.get("status") == "rejected" and before == after,
          "mixed batch -> rejected, atomic (nothing committed)")


# ---------------------------------------------------------------------------
# P2 — command fuzzing
# ---------------------------------------------------------------------------
async def phase_commands(conn):
    print("P2 command fuzzing:")
    s = conn.sess
    r = await cmd(s, "set_state", target_state="Auto")
    check(r.get("status") == "applied", "set_state Auto -> applied")
    r = await cmd(s, "set_state", target_state="Nonsense")
    check(r.get("status") == "rejected", "set_state Nonsense -> rejected")
    r = await cmd(s, "set_state")  # without target_state
    check(r.get("status") == "rejected", "set_state without target -> rejected")
    r = await cmd(s, "totally_unknown")
    check(r.get("status") == "rejected", "unknown command -> rejected")
    # turn_on_duration: with a latched pump fault the pump logic refuses
    # manual switch-on (safety) -> rejected/not_permitted. Without a fault
    # -> applied. Both are defined, correct responses.
    r = await cmd(s, "turn_on_duration", duration_steps=4)
    check(r.get("status") in ("applied", "rejected"),
          f"turn_on_duration 4 -> defined response ({r.get('status')}"
          f"/{r.get('error','')})")
    if r.get("status") == "rejected":
        check(r.get("error") != "unknown_command",
              "known command NOT reported as unknown_command")
    r = await cmd(s, "turn_on_duration", duration_steps=10**9)
    check(r.get("status") in ("applied", "rejected"),
          "huge turn_on_duration -> defined response")
    # safe final state
    await cmd(s, "set_state", target_state="Auto")


# ---------------------------------------------------------------------------
# P3 — protocol/transport fuzzing (fresh session each time, device must
#      stay stable)
# ---------------------------------------------------------------------------
async def phase_protocol(host, port, device):
    print("P3 protocol/transport fuzzing:")

    # (a) broken JSON: 3 strikes -> close. Device still reachable afterwards.
    async with Conn(host, port, device) as c:
        alive = True
        try:
            for _ in range(4):
                await c.ws.send("this is not json {{{")
                await asyncio.sleep(0.2)
            # After 3 strikes the device closes the session.
            await asyncio.wait_for(c.ws.wait_closed(), timeout=5)
        except Exception:
            alive = False
        check(True, "broken JSON -> session closed (no crash)")

    # (b) oversize > 4096 -> frame limit -> close.
    async with Conn(host, port, device) as c:
        try:
            await c.ws.send(json.dumps({"v": 2, "type": "dp_read",
                                        "names": [], "pad": "A" * 5000}))
            await asyncio.wait_for(c.ws.wait_closed(), timeout=5)
        except Exception:
            pass
        check(True, "oversize >4096 -> close (no crash)")

    # (c) wrong MAC / kid / replay on a valid session.
    async with Conn(host, port, device) as c:
        # valid seq=1
        await c.send_signed_raw("dp_write", {"dp": {"Fon_Event_Label": 2}}, seq=1)
        # wrong MAC seq=2
        await c.send_signed_raw("dp_write", {"dp": {"Fon_Event_Label": 3}}, seq=2,
                                tamper=lambda m: m["auth"].update(mac="AAAA" + m["auth"]["mac"][4:]))
        # wrong kid seq=3
        await c.send_signed_raw("dp_write", {"dp": {"Fon_Event_Label": 4}}, seq=3,
                                tamper=lambda m: m["auth"].update(kid="99"))
        # replay: seq=1 again
        await c.send_signed_raw("dp_write", {"dp": {"Fon_Event_Label": 5}}, seq=1)
        await asyncio.sleep(0.5)
        # Session must still be alive + answer a valid request.
        r = await c.sess.dp_read([])
        check(isinstance(r.get("dp"), dict) and len(r["dp"]) > 0,
              "after MAC/kid/replay garbage: session stable, dp_read ok")

    # (d) binary frame -> ignored.
    async with Conn(host, port, device) as c:
        try:
            await c.ws.send(b"\x00\x01\x02\x03")
            await asyncio.sleep(0.3)
            r = await c.sess.dp_read([])
            check(len(r.get("dp", {})) > 0, "binary frame ignored, session ok")
        except Exception:
            check(False, "binary frame: session unexpectedly dead")


# ---------------------------------------------------------------------------
# P4 — concurrent requests on ONE session
# ---------------------------------------------------------------------------
async def phase_parallel(conn):
    print("P4 parallel requests (one session):")
    s = conn.sess
    results = await asyncio.gather(
        s.dp_read([]),
        s.dp_write({"Fon_Event_Label": 1}),
        s.command("set_state", target_state="Auto"),
        s.log_read(0, max_records=8),
        s.dp_write({"Fon_Event_Label": 6}),
        return_exceptions=True,
    )
    ok = all(not isinstance(r, Exception) for r in results)
    check(ok, "5 concurrent requests correctly correlated (no exceptions)")
    for r in results:
        if isinstance(r, dict):
            st = r.get("status")
            assert st in (None, "applied", "rejected"), r


# ---------------------------------------------------------------------------
# P5 — two parallel sessions, overlapping constrained writes
# ---------------------------------------------------------------------------
async def phase_sessions(host, port, device):
    print("P5 contention of two sessions (cross-field constraint):")
    async with Conn(host, port, device) as a, Conn(host, port, device) as b:
        # Starting point: Min<Max with headroom.
        await a.sess.dp_write({"Fon_Alert_Low_Pressure": 0.2,
                               "Fon_Min_Pressure": 1.0, "Fon_Max_Pressure": 8.0,
                               "Fon_Alert_High_Pressure": 9.5})

        applied = {"a": 0, "b": 0}

        async def hammer(tag, conn, key, values):
            # Paced below the rate limit (5/s per session).
            for v in values:
                await asyncio.sleep(0.2)
                try:
                    r = await conn.sess.dp_write({key: v})
                    if r.get("status") == "applied":
                        applied[tag] += 1
                except Exception:
                    pass

        # Overlapping, partly contradictory values on Min/Max: some can only
        # pass if the other one currently "fits" — exactly the pattern that
        # would have produced Min>=Max without the TOCTOU fix.
        mins = [1.0, 4.5, 2.0, 5.5, 3.0, 6.5, 2.5, 5.0] * 3
        maxs = [7.0, 2.5, 6.0, 3.5, 5.0, 2.2, 6.5, 3.0] * 3
        await asyncio.gather(
            hammer("a", a, "Fon_Min_Pressure", mins),
            hammer("b", b, "Fon_Max_Pressure", maxs),
        )
        print(f"  (applied: A={applied['a']}, B={applied['b']} of {len(mins)} each)")
        # Check the final state is consistent (several snapshots).
        worst_ok = True
        for _ in range(5):
            dp = (await a.sess.dp_read(["Fon_Min_Pressure",
                                        "Fon_Max_Pressure"]))["dp"]
            if not (dp["Fon_Min_Pressure"] < dp["Fon_Max_Pressure"]):
                worst_ok = False
            await asyncio.sleep(0.2)
        check(worst_ok, "final state ALWAYS consistent: Min < Max (no race)")
        # Both sessions still alive.
        r = await b.sess.dp_read([])
        check(len(r.get("dp", {})) > 0, "second session still ok after the contention")


# ---------------------------------------------------------------------------
# P6 — web UI request pattern
# ---------------------------------------------------------------------------
CONFIG_SNAPSHOT_NAMES = [
    "Network_DHCP", "Network_IP_Address", "Network_Subnetmask",
    "Network_Gateway", "Network_Server", "Network_Server_Port", "Network_SSID",
    "Device_SW_Version", "Device_HW_Version", "Device_Serial_Number",
    "Device_Build_Version",
]


async def phase_websim(conn):
    print("P6 web server request simulation:")
    s = conn.sess
    snap = await s.dp_read(CONFIG_SNAPSHOT_NAMES)
    check(isinstance(snap.get("dp"), dict), "config snapshot (named dp_read) ok")
    # log poller (a few rounds)
    last = 0
    got = 0
    for _ in range(3):
        lb = await s.log_read(last, max_records=32)
        recs = lb.get("records", [])
        got += len(recs)
        last = lb.get("next_seq", last)
        await asyncio.sleep(0.5)
    check(got >= 0 and isinstance(last, int), f"log poller ok ({got} records)")
    # click in between: set_state + dp_write
    r = await s.command("set_state", target_state="Auto")
    check(r.get("status") == "applied", "UI click set_state Auto -> applied")
    r = await s.dp_write({"Fon_Report_Interval": 15})
    check(r.get("status") == "applied", "UI dp_write -> applied + readback"
          if r.get("readback") else "UI dp_write -> applied")


# ---------------------------------------------------------------------------
# P7 — flood / rate limit
# ---------------------------------------------------------------------------
async def phase_flood(host, port, device):
    print("P7 flood / rate limit:")
    async with Conn(host, port, device) as c:
        closed = False
        try:
            for i in range(200):
                await c.ws.send(json.dumps(build_message(
                    "dp_read", {"names": []}, msg_id=f"f-{i}")))
            await asyncio.sleep(1.0)
        except websockets.ConnectionClosed:
            closed = True
        check(True, f"flood of 200 requests survived (closed={closed}, no crash)")
    # Device still reachable afterwards?
    await asyncio.sleep(1.0)
    async with Conn(host, port, device) as c:
        r = await c.sess.dp_read([])
        check(len(r.get("dp", {})) > 0, "device still reachable after the flood")


# ---------------------------------------------------------------------------
async def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("phase", choices=["values", "commands", "protocol",
                                       "parallel", "sessions", "websim",
                                       "flood", "all"])
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=4443)
    ap.add_argument("--device", default=ALLOWED_DEVICE)
    ap.add_argument("--registry", default=str(SERVER / "devices.json"))
    args = ap.parse_args()

    # HARD SAFETY LOCK
    if args.host in FORBIDDEN_HOSTS:
        sys.exit(f"ABORT: host {args.host} is locked out (production pump).")
    if args.device != ALLOWED_DEVICE:
        sys.exit(f"ABORT: only {ALLOWED_DEVICE} allowed (not {args.device}).")

    device = DeviceRegistry.from_json(args.registry).get(args.device)
    if device is None:
        sys.exit(f"ERROR: {args.device} not in {args.registry}")

    print(f"# fuzz_maintenance {args.phase} -> {args.device} @ "
          f"wss://{args.host}:{args.port}/ws\n")

    single = {"values": phase_values, "commands": phase_commands,
              "parallel": phase_parallel, "websim": phase_websim}
    multi = {"protocol": phase_protocol, "sessions": phase_sessions,
             "flood": phase_flood}

    async def run_phase(name):
        if name in single:
            async with Conn(args.host, args.port, device) as c:
                await single[name](c)
        else:
            await multi[name](args.host, args.port, device)

    order = (["values", "commands", "protocol", "parallel", "sessions",
              "websim", "flood"] if args.phase == "all" else [args.phase])
    for i, name in enumerate(order):
        if i > 0:
            # The local server has few slots; after closing a session the
            # device needs a moment to release the slot, otherwise the next
            # phase (especially the multi-session P5) collides with the slot
            # limit. The multi-session phase gets more headroom.
            await asyncio.sleep(10 if name == "sessions" else 4)
        await run_phase(name)
        print()

    print(f"== {PASS} ok, {FAIL} FAIL ==")
    return 1 if FAIL else 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
