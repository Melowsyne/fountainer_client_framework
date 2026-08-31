#!/usr/bin/env python3
# Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
# SPDX-License-Identifier: MIT
#
# fake_device.py — emulates the firmware's LOCAL WSS server for integration
# tests of the C++ framework when no device is available.
#
# Behaves like src/network/local_server + clientside_protocol:
#   - WSS + mTLS, subprotocol "fountain", 1 slot
#   - device sends hello -> expects hello_ack -> sends signed
#     ota_check proof -> expects ota_none -> RUNNING
#   - dp_read (explicit names OR empty list = VOLATILE only!),
#     dp_write (batch, atomic, readback), command, log_read/log_read_prev/
#     log_ack_prev — responses mirror task_com.c
#   - optionally sends spontaneous dp_reports/device_alerts (--push)
#
# Protocol/auth reference: fountain_proto (fountainer_server) — the same
# library the firmware was verified against.
#
# Invocation (certificates = testbed PKI):
#   python3 tools/fake_device.py --port 4443 \
#       --ca .../ca.crt.pem --cert .../server.crt.pem --key .../server.key.pem \
#       [--key-password ...] [--push]
from __future__ import annotations

import argparse
import asyncio
import json
import math
import os
import ssl
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..",
                                "fountainer_server"))
from fountain_proto import auth as fp_auth          # noqa: E402
from fountain_proto.envelope import build_message   # noqa: E402

DEVICE_ID = "esp32-fake01"
SERIAL = "000001C0C01FA82A"
KID = "1"
KEY = bytes(range(32))   # testbed golden key 000102...1f
FW = "4.34.0-fake"

sys.path.insert(0, os.path.dirname(__file__))

# Derive the datapoint store from the client's generated catalog — a single
# source of truth for the fake as well.
import re                                            # noqa: E402

GENERATED = os.path.join(os.path.dirname(__file__), "..",
                         "include/fountainer/datapoints/generated.hpp")
DESC_RE = re.compile(
    r'\{(\d+), "([A-Za-z0-9_]+)", DatapointType::(\w+), Access::(\w+), '
    r'Persistence::(\w+), (\d+), ([^,]+), ([^,]+), ([^,]+), \{')


def parse_catalog():
    points = {}
    with open(GENERATED, encoding="utf-8") as f:
        for line in f:
            m = DESC_RE.search(line)
            if not m:
                continue
            idx, name, typ, access, persist, nvs, default, lo, hi = m.groups()

            def num(token):
                token = token.strip()
                if token == "kDatapointUnbounded":
                    return math.nan
                return float(token)

            points[name] = {
                "type": typ, "access": access, "persist": persist,
                "default": num(default), "min": num(lo), "max": num(hi),
            }
    if len(points) < 100:
        raise SystemExit("generated.hpp not parsable")
    return points


CATALOG = parse_catalog()


def initial_value(name, meta):
    if name == "Device_Serial_Number":
        return SERIAL
    if meta["type"] == "Str":
        return "fake" if meta["access"] == "ReadWrite" else FW
    if meta["type"] == "U64":
        return "0000000000000000"
    if meta["type"] == "Bool":
        return bool(int(meta["default"]) if not math.isnan(meta["default"]) else 0)
    d = meta["default"]
    if math.isnan(d):
        d = 0.0
    return d if meta["type"] == "F32" else int(d)


class FakeDevice:
    def __init__(self, push: bool):
        self.push = push
        self.store = {n: initial_value(n, m) for n, m in CATALOG.items()}
        self.log_seq = 1
        self.logs = [self._record(i) for i in range(1, 200)]
        self.busy = asyncio.Lock()   # 1 slot like LOCAL_SERVER_MAX_CLIENTS=1

    def _record(self, seq):
        return {"s": seq, "u": 1000 + seq, "ev": 7, "mod": 2, "lvl": 3,
                "t": f"fake record {seq}"}

    # ----- dp_write with firmware validation ------------------------------
    def apply_write(self, dp: dict) -> dict:
        result: dict = {}
        batch = dict(dp)
        network_save = batch.pop("Network_Save", None)
        batch.pop("Log_Command", None)

        errors = {}
        staged = {}
        for name, value in batch.items():
            meta = CATALOG.get(name)
            if meta is None:
                errors[name] = "unknown_name"
                continue
            if meta["access"] != "ReadWrite":
                errors[name] = "read_only"
                continue
            if meta["type"] == "Bool":
                if not isinstance(value, bool):
                    errors[name] = "type_mismatch"
                    continue
                staged[name] = value
            elif meta["type"] == "Str":
                if not isinstance(value, str):
                    errors[name] = "type_mismatch"
                    continue
                if len(value) >= 64:
                    errors[name] = "too_long"
                    continue
                staged[name] = value
            elif meta["type"] == "U64":
                if not isinstance(value, str):
                    errors[name] = "type_mismatch"
                    continue
                staged[name] = value
            else:
                if isinstance(value, bool) or not isinstance(value, (int, float)):
                    errors[name] = "type_mismatch"
                    continue
                if not math.isnan(meta["min"]) and value < meta["min"]:
                    errors[name] = "out_of_range"
                    continue
                if not math.isnan(meta["max"]) and value > meta["max"]:
                    errors[name] = "out_of_range"
                    continue
                staged[name] = value if meta["type"] == "F32" else int(value)

        if errors:
            result["status"] = "rejected"
            result["errors"] = errors
            return result

        self.store.update(staged)   # atomic
        if network_save is not None:
            result["network_save"] = int(network_save)
        result["status"] = "applied"
        result["readback"] = {name: self.store[name] for name in dp
                              if name in self.store}
        return result

    def apply_command(self, msg: dict) -> dict:
        command = msg.get("command")
        target = msg.get("target_state")
        if command == "set_state" and target in ("On", "Off", "Auto", "Manual"):
            self.store["Fon_Relay_Output"] = target == "On"
            return {"status": "applied"}
        if command in ("turn_on_duration", "restart", "reboot", "wd_fault",
                       "link_fault"):
            return {"status": "applied"}
        return {"status": "rejected", "error": "unknown_command"}

    def fill_log_batch(self, since, max_records, prev):
        if prev:
            return {"boot_id": 41, "available": True,
                    "records": self.logs[:min(8, max_records)]}
        records = [r for r in self.logs if r["s"] > since]
        # Simulate the byte budget: at most 40 records per batch.
        records = records[:min(max_records, 40)]
        return {
            "boot_id": 42,
            "first_seq_available": self.logs[0]["s"],
            "next_seq": self.logs[-1]["s"] + 1,
            "dropped_count": 0,
            "overflow": False,
            "records": records,
        }

    # ----- one connection -------------------------------------------------
    async def session(self, ws):
        if self.busy.locked():
            print("[fake] slot busy -> reject", flush=True)
            await ws.close()
            return
        async with self.busy:
            await self._session(ws)

    async def _session(self, ws):
        server_nonce = None
        client_nonce = "RkFLRV9DTElFTlRfTk9OQ0U="
        c2s_seq = 0
        replay = fp_auth.AntiReplay()

        def now_ms():
            return int(time.time() * 1000)

        def send_signed(name, body, msg_id=None, in_reply_to=None):
            nonlocal c2s_seq
            msg = build_message(name, body, serial=SERIAL, msg_id=msg_id,
                                in_reply_to=in_reply_to, ts=now_ms())
            meta_needs_auth = name in ("ota_check",)
            if meta_needs_auth:
                c2s_seq += 1
                fp_auth.sign(msg, auth_key=KEY, kid=KID, seq=c2s_seq,
                             direction="c2s", device_id=DEVICE_ID,
                             server_nonce=server_nonce,
                             client_nonce=client_nonce)
            return ws.send(json.dumps(msg, separators=(",", ":")))

        # 1) hello
        hello = build_message("hello", {
            "device_id": DEVICE_ID, "protocol_version": 2, "fw_version": FW,
            "hw_rev": "0.10.0", "boot_reason": "power_on",
            "auth_schemes": ["hmac-sha256"], "auth_kids": [KID],
            "client_nonce": client_nonce,
        }, serial=SERIAL, msg_id="hello-1", ts=now_ms())
        await ws.send(json.dumps(hello, separators=(",", ":")))

        # 2) hello_ack
        ack = json.loads(await ws.recv())
        if ack.get("type") != "hello_ack" or not ack.get("accepted"):
            print("[fake] hello_ack rejected:", ack, flush=True)
            await ws.close(4004)
            return
        server_nonce = ack.get("server_nonce")

        # 3) signed proof
        await send_signed("ota_check", {"current_version": FW}, msg_id="chk-1")
        decision = json.loads(await ws.recv())
        if decision.get("type") not in ("ota_none", "ota_available"):
            print("[fake] unexpected ota reply:", decision, flush=True)
            await ws.close(4000)
            return
        print("[fake] session running", flush=True)

        push_task = None
        if self.push:
            push_task = asyncio.create_task(self._pusher(ws))
        try:
            async for raw in ws:
                msg = json.loads(raw)
                mtype = msg.get("type")
                reply_to = msg.get("msg_id")

                def verified():
                    if "auth" not in msg:
                        return False
                    ok, reason = fp_auth.verify(
                        msg, auth_key=KEY, expected_kid=KID, direction="s2c",
                        device_id=DEVICE_ID, server_nonce=server_nonce,
                        client_nonce=client_nonce)
                    if not ok:
                        print("[fake] bad auth:", reason, flush=True)
                        return False
                    return replay.check(msg["auth"]["seq"])

                if mtype == "dp_read":
                    names = msg.get("names") or []
                    if names:
                        dp = {}
                        unknown = []
                        for n in names:
                            if n in self.store:
                                dp[n] = self.store[n]
                            else:
                                unknown.append(n)
                        body = {"dp": dp}
                        if unknown:
                            body["unknown"] = unknown
                    else:
                        # Firmware quirk: empty list = VOLATILE only!
                        body = {"dp": {n: v for n, v in self.store.items()
                                       if CATALOG[n]["persist"] == "Volatile"}}
                    out = build_message("dp_report", body, serial=SERIAL,
                                        in_reply_to=reply_to, ts=now_ms())
                    await ws.send(json.dumps(out, separators=(",", ":")))
                elif mtype == "dp_write":
                    result = (self.apply_write(msg.get("dp") or {}) if verified()
                              else {"status": "rejected", "error": "auth"})
                    out = build_message("dp_write_result", result, serial=SERIAL,
                                        in_reply_to=reply_to, ts=now_ms())
                    await ws.send(json.dumps(out, separators=(",", ":")))
                elif mtype == "command":
                    result = (self.apply_command(msg) if verified()
                              else {"status": "rejected", "error": "auth"})
                    out = build_message("command_result", result, serial=SERIAL,
                                        in_reply_to=reply_to, ts=now_ms())
                    await ws.send(json.dumps(out, separators=(",", ":")))
                elif mtype in ("log_read", "log_read_prev"):
                    if not verified():
                        continue
                    body = self.fill_log_batch(msg.get("since_seq", 0),
                                               min(int(msg.get("max_records", 64)),
                                                   128),
                                               mtype == "log_read_prev")
                    out = build_message("log_batch", body, serial=SERIAL,
                                        in_reply_to=reply_to, ts=now_ms())
                    await ws.send(json.dumps(out, separators=(",", ":")))
                elif mtype == "log_ack_prev":
                    ok = verified()
                    out = build_message("log_ack_result", {"ok": bool(ok)},
                                        serial=SERIAL, in_reply_to=reply_to,
                                        ts=now_ms())
                    await ws.send(json.dumps(out, separators=(",", ":")))
                else:
                    print("[fake] ignoring", mtype, flush=True)
        finally:
            if push_task:
                push_task.cancel()
            print("[fake] session closed", flush=True)

    async def _pusher(self, ws):
        seq = 0
        while True:
            await asyncio.sleep(3)
            seq += 1
            self.store["System_Uptime"] = int(self.store["System_Uptime"]) + 3
            report = build_message("dp_report", {
                "seq": seq,
                "dp": {"System_Uptime": self.store["System_Uptime"]},
            }, serial=SERIAL, ts=int(time.time() * 1000))
            await ws.send(json.dumps(report, separators=(",", ":")))
            if seq % 5 == 0:
                alert = build_message("device_alert", {
                    "code": "TEST_ALERT", "severity": "info",
                    "detail": "fake alert " + str(seq),
                }, serial=SERIAL, ts=int(time.time() * 1000))
                await ws.send(json.dumps(alert, separators=(",", ":")))


async def main():
    import websockets

    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=4443)
    parser.add_argument("--ca", required=True)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--key-password", default=None)
    parser.add_argument("--push", action="store_true",
                        help="send spontaneous dp_reports/device_alerts")
    args = parser.parse_args()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(args.cert, args.key, password=args.key_password)
    ctx.load_verify_locations(args.ca)
    ctx.verify_mode = ssl.CERT_REQUIRED   # mTLS like the firmware

    device = FakeDevice(push=args.push)
    async with websockets.serve(device.session, args.host, args.port, ssl=ctx,
                                subprotocols=["fountain"], max_size=64 * 1024):
        print(f"[fake] {DEVICE_ID} listening on wss://{args.host}:{args.port}/ws",
              flush=True)
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
