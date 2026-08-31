#!/usr/bin/env bash
# Entrypoint of the client container: configures and builds from the mounted
# source code (/app) into the named volume /app/build and then runs the
# requested command.
#
#   build             -> compile only
#   test              -> compile + unit tests (ctest)
#   run [args...]     -> compile + start fountainer-cli
#   runonly [args...] -> start ONLY (no build) — for multi-client: `build`
#                        once, then N containers with `runonly` (no
#                        parallel build races on the fcf_build volume).
#
# CLI invocation: fountainer-cli $CLIENT_CONFIG <args>; without arguments
# `watch` is started (default polling, runs until SIGINT — matches the
# former continuous operation of the Application). The campaign configs with
# fountain.sequence run with `script`.
set -euo pipefail

cmd="${1:-run}"
build_type="${BUILD_TYPE:-Debug}"

if [ "${cmd}" != "runonly" ]; then
    cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE="${build_type}" >/dev/null
    cmake --build /app/build -j"$(nproc)"
fi

case "${cmd}" in
    build)
        ;;
    test)
        ctest --test-dir /app/build --output-on-failure
        ;;
    run|runonly)
        shift || true
        if [ "$#" -eq 0 ]; then
            set -- watch
        fi
        # exec: the CLI becomes PID 1 and receives SIGINT/SIGTERM directly
        exec /app/build/fountainer-cli \
            "${CLIENT_CONFIG:-/app/config/client.docker.json}" "$@"
        ;;
    *)
        echo "unknown command: ${cmd} (expected build|test|run|runonly)" >&2
        exit 2
        ;;
esac
