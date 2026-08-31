#!/usr/bin/env bash
# Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
# SPDX-License-Identifier: MIT
#
# Integration test of the framework against the fake device (tools/fake_device.py,
# device role with fountain_proto as the protocol reference).
# Runs on the host (needs docker compose + python3-websockets); invocation:
#     bash tools/integration_test.sh
#
# Scenarios:
#   1. unit tests in the container
#   2. connect() == Fountain RUNNING; info returns the device identity
#   3. read-all (107 explicit names)
#   4. write + readback, remote rejection as the result
#   5. log pull with pagination (truncated batches)
#   6. command set_state
#   7. reconnect after server restart
set -euo pipefail

cd "$(dirname "$0")/.."

CA="../DO_NOT_COMMIT/CA/root/certs/ca.crt.pem"
CERT="../DO_NOT_COMMIT/CA/server/server.crt.pem"
KEY="../DO_NOT_COMMIT/CA/server/server.key.pem"
KEY_PW="${TLS_KEY_PASSWORD:-server_password}"
PORT=4444
CFG=/app/config/client.fake.json

fail() { echo "FAIL: $*" >&2; cleanup; exit 1; }
ok()   { echo "OK:   $*"; }

FAKE_PID=""
cleanup() {
    [ -n "${FAKE_PID}" ] && kill "${FAKE_PID}" 2>/dev/null || true
    docker rm -f fcf_it_watch >/dev/null 2>&1 || true
}
trap cleanup EXIT

start_fake() {
    python3 tools/fake_device.py --port "${PORT}" --ca "${CA}" --cert "${CERT}" \
        --key "${KEY}" --key-password "${KEY_PW}" --push \
        > /tmp/fake_device.log 2>&1 &
    FAKE_PID=$!
    sleep 2
    kill -0 "${FAKE_PID}" || fail "fake_device does not start (see /tmp/fake_device.log)"
}

cli() {
    CLIENT_CONFIG="${CFG}" docker compose run --rm client runonly "$@" 2>&1
}

echo "== Build =="
docker compose build >/dev/null
docker compose run --rm client build >/dev/null || fail "Build"

echo "== 1. Unit tests =="
docker compose run --rm client test >/dev/null || fail "unit tests"
ok "unit tests"

start_fake

echo "== 2. connect/info =="
out="$(cli info)" || fail "info"
echo "${out}" | grep -q "device_id: esp32-fake01" || fail "info: identity missing"
echo "${out}" | grep -q "session established" || fail "info: no Fountain RUNNING"
ok "connect() == RUNNING, ConnectionInfo complete"

echo "== 3. read-all =="
out="$(cli read-all)" || fail "read-all"
echo "${out}" | grep -q "^107 datapoints" || fail "read-all: not 107 datapoints"
ok "read-all delivers all 107 datapoints"

echo "== 4. write =="
out="$(cli write Fon_Min_Pressure=2.2)" || fail "write"
echo "${out}" | grep -q "applied" || fail "write: not applied"
echo "${out}" | grep -q "Fon_Min_Pressure = 2.20 bar" || fail "write: readback missing"
out="$(cli write Fon_Event_Label=99 || true)"
echo "${out}" | grep -q "out_of_range" || fail "write: out_of_range not detected"
ok "write with readback + validation"

echo "== 5. logs (Pagination) =="
out="$(cli logs all)" || fail "logs all"
echo "${out}" | grep -q "^199 records" || fail "logs: pagination incomplete"
ok "199 records across truncated batches"

echo "== 6. command =="
out="$(cli command set_state Off)" || fail "command"
echo "${out}" | grep -q "set_state: applied" || fail "command: not applied"
ok "command set_state Off"

echo "== 7. Reconnect =="
CLIENT_CONFIG="${CFG}" docker compose run --name fcf_it_watch -d client \
    runonly watch 1000 System_Uptime >/dev/null 2>&1
sleep 4
kill "${FAKE_PID}"; FAKE_PID=""
sleep 3
start_fake
sleep 8
log="$(docker logs fcf_it_watch 2>&1)"
echo "${log}" | grep -q "reconnect in" || fail "reconnect not triggered"
[ "$(echo "${log}" | grep -c 'session established')" -ge 2 ] || \
    fail "session not re-established after reconnect"
docker kill --signal=INT fcf_it_watch >/dev/null 2>&1 || true
sleep 1
ok "reconnect + new handshake"

echo
echo "ALL SCENARIOS GREEN"
