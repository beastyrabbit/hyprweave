#!/usr/bin/env bash
# Records the README screenshots and videos. Runs in the CI VM, which gives a clean 1920x1080
# output with nothing else on it:
#
#   docker run --rm --device /dev/kvm -v "$PWD:/src" -w /src \
#       -e EXTRA_PACKAGES="mpv wf-recorder bat btop ttf-jetbrains-mono" \
#       archlinux:latest tests/ci/run.sh dev/media/record.sh
#
# Raw files land in ci-logs; dev/media/encode.sh turns them into docs/media.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
N="$ROOT/dev/nest"
# the VM can only write to ci-logs itself: new directories in it are read-only for this user
OUT="${LOGS:?run this through tests/ci/run.sh}"
export WAYLAND_DISPLAY
WAYLAND_DISPLAY=$("$N" wayland)

ctl() { "$N" ctl "$@" >/dev/null; }
lua() { "$N" ctl repl "$1" >/dev/null; }
exec_cmd() { ctl dispatch "hl.dsp.exec_cmd(\"$1\")"; }
wait_class() { for _ in $(seq 1 40); do "$N" ctl clients -j | jq -e --arg c "$1" 'any(.[]; .class == $c)' >/dev/null && return; sleep 0.25; done; }
close_all() {
    "$N" ctl clients -j | jq -r '.[].address' | while read -r a; do ctl dispatch "hl.dsp.window.close({ window = \"address:$a\" })"; done
    sleep 1.5
}

KITTY="kitty -o font_family='JetBrains Mono' -o font_size=12 -o background=#1e1e2e -o foreground=#cdd6f4 -o window_padding_width=10 -o confirm_os_window_close=0"
stream() { exec_cmd "mpv --vo=wlshm --wayland-app-id=demo-stream --no-osc --no-input-default-bindings --loop=inf av://lavfi:testsrc2=s=1280x720:r=30 >$OUT/mpv.log 2>&1"; wait_class demo-stream; }
chat() { exec_cmd "$KITTY --class demo-chat $ROOT/dev/media/chat.sh"; wait_class demo-chat; }
code() { exec_cmd "$KITTY --class demo-lua sh -c 'bat --style=plain --color=always --theme=ansi --paging=never $ROOT/dev/media/group.lua; sleep infinity'"; wait_class demo-lua; }
editor() { exec_cmd "$KITTY -o font_size=11 --class demo-editor sh -c 'bat --style=numbers --color=always --theme=ansi --paging=never --wrap=never --line-range=14:29 $ROOT/src/Tree.cpp; sleep infinity'"; wait_class demo-editor; }
monitor() { exec_cmd "$KITTY -o font_size=9 --class demo-top btop"; wait_class demo-top; }
group() { lua "$(grep -v "^--" "$ROOT/dev/media/group.lua")"; } # hyprctl would read "--" as a flag

record() { wf-recorder -y -c libx264 -p preset=ultrafast -p crf=16 -r 30 -f "$OUT/$1.mp4" >"$OUT/wf-recorder-$1.log" 2>&1 & REC=$!; sleep 1; }
stop_recording() { sleep 1; kill -INT "$REC"; wait "$REC" || true; }

"$N" reload >/dev/null
ctl eval 'hl.config({
    general = { gaps_in = 6, gaps_out = 14, border_size = 2, layout = "dwindle",
        col = { active_border = { colors = { "rgba(cba6f7ff)", "rgba(89b4faff)" }, angle = 45 }, inactive_border = "rgba(45475aff)" } },
    group = { col = { border_active = "rgba(89b4faff)", border_inactive = "rgba(45475aff)",
        border_locked_active = "rgba(89b4faff)", border_locked_inactive = "rgba(45475aff)" } },
    decoration = { rounding = 12 },
    misc = { background_color = "rgb(11111b)" },
    cursor = { inactive_timeout = 1 },
    plugin = { hyprweave = { frame_size = 3, frame_color_active = "rgb(cba6f7)", frame_color_inactive = "rgb(585b70)" } },
})'
close_all

# 1: three windows become a group; the group keeps its split in every layout
stream
chat
code
sleep 2
record create
sleep 1.5
group
sleep 2.5
for layout in master scrolling dwindle; do
    ctl eval "hl.config({ general = { layout = \"$layout\" } })"
    sleep 2
done
stop_recording

ctl dispatch 'hl.dsp.focus({ window = "class:^demo-stream$" })'
sleep 1.5
grim "$OUT/group.png"

# 2: a member alone in fullscreen, the whole group in fullscreen, then closing it
record fullscreen
sleep 1
ctl dispatch 'hl.dsp.window.fullscreen()'
sleep 2.5
ctl dispatch 'hl.dsp.window.fullscreen()'
sleep 2
lua 'return hl.plugin.hyprweave.fullscreen()'
sleep 2.5
lua 'return hl.plugin.hyprweave.fullscreen()'
sleep 2
ctl dispatch 'hl.dsp.window.close({ window = "class:^demo-stream$" })'
sleep 2.5
stop_recording
close_all

# nested containers with a background tint
editor
monitor
chat
lua 'return hl.plugin.hyprweave.create({ direction = "row", gap = 10, background = "rgba(cba6f730)", children = {
    { window = "class:^demo-editor$", size = 58 },
    { size = 42, direction = "column", children = {
        { window = "class:^demo-top$", size = 60 },
        { window = "class:^demo-chat$", size = 40 },
    } },
} })'
ctl dispatch 'hl.dsp.focus({ window = "class:^demo-editor$" })'
sleep 3
grim "$OUT/nested.png"
close_all
