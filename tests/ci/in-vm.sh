#!/usr/bin/env bash
# Runs inside the VM that tests/ci/run.sh boots, as root: starts a seat, then Hyprland (with
# dev/nested.lua) and COMMAND as an ordinary user. Writes COMMAND's exit status to ci-logs/status.
#
#   tests/ci/in-vm.sh COMMAND
set -uo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
LOGS="$ROOT/ci-logs"
COMMAND=$1

seatd -g tester >"$LOGS/seatd.log" 2>&1 &
sleep 1

runuser -u tester -- env ROOT="$ROOT" LOGS="$LOGS" COMMAND="$COMMAND" bash -c '
    export XDG_RUNTIME_DIR=/tmp/xdg-tester
    mkdir -p -m 700 "$XDG_RUNTIME_DIR"
    # seatd gives Hyprland the virtual GPU; Mesa renders in software
    export LIBSEAT_BACKEND=seatd LIBGL_ALWAYS_SOFTWARE=1

    "$ROOT/dev/nest" start --bare >"$LOGS/hyprland-stdout.log" 2>&1 &
    for _ in $(seq 1 60); do
        "$ROOT/dev/nest" ctl version >/dev/null 2>&1 && break
        sleep 0.5
    done
    "$ROOT/dev/nest" ctl version | head -1

    "$COMMAND"
    echo $? >"$LOGS/status"

    cp "$XDG_RUNTIME_DIR"/hypr/*/hyprland.log "$LOGS/" 2>/dev/null
    "$ROOT/dev/nest" stop >/dev/null 2>&1
'
