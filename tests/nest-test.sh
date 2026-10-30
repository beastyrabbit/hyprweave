#!/usr/bin/env bash
# End-to-end checks for hyprweave in the nested session. Needs a running nest
# (`dev/nest start` in another terminal) and kitty. Loads the current build, opens test windows
# with unique classes, checks geometry and behaviour through hyprctl, and closes its windows.
#
#   tests/nest-test.sh
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
N="$ROOT/dev/nest"
FAILURES=0

ctl() { "$N" ctl "$@"; }
lua() { ctl repl "$1"; }
pass() { echo "  ok   $1"; }
fail() {
    echo "  FAIL $1" >&2
    FAILURES=$((FAILURES + 1))
}
check() { # check "description" <command...>
    local desc=$1
    shift
    if "$@"; then pass "$desc"; else fail "$desc"; fi
}

client() { ctl clients -j | jq -c --arg c "$1" '.[] | select(.class == $c)'; }
field() { client "$1" | jq -r "$2"; }
count() { ctl clients -j | jq --arg p "$1" '[.[] | select(.class | startswith($p))] | length'; }

# predicates for wait_for, which re-runs them; their arguments must not be pre-expanded queries
exists() { [[ -n $(client "$1") ]]; }
gone() { [[ -z $(client "$1") ]]; }
ungrouped() { [[ $(field "$1" '.grouped | length') -eq 0 ]]; }
no_test_windows() { [[ $(count hw-) -eq 0 ]]; }

wait_for() { # wait_for <seconds> <command...>
    local tries=$(($1 * 4))
    shift
    for _ in $(seq 1 "$tries"); do
        "$@" && return 0
        sleep 0.25
    done
    return 1
}

open() { # open <class>...
    for c in "$@"; do
        ctl dispatch "hl.dsp.exec_cmd(\"kitty --class $c\")" >/dev/null
    done
    for c in "$@"; do
        wait_for 10 exists "$c" || {
            echo "window $c did not open" >&2
            exit 1
        }
    done
    sleep 0.5
}

close_all() {
    ctl clients -j | jq -r '.[] | select(.class | startswith("hw-")) | .address' | while read -r a; do
        ctl dispatch "hl.dsp.window.close({ window = \"address:$a\" })" >/dev/null
    done
    wait_for 10 no_test_windows
}

# ratio <a> <b> <axis: w|h> -> share of a in percent
ratio() {
    local i=0
    [[ $3 == h ]] && i=1
    local a b
    a=$(field "$1" ".size[$i]")
    b=$(field "$2" ".size[$i]")
    echo $((100 * a / (a + b)))
}
near() { (($1 >= $2 - 2 && $1 <= $2 + 2)); }
ratio_near() { near "$(ratio "$1" "$2" "$3")" "$4"; } # predicate for wait_for
groups() { ctl -j hyprweave | jq '.groups | length'; }

trap close_all EXIT

echo "== load"
"$N" reload >/dev/null
check "plugin loaded" test "$(ctl -j hyprweave | jq -r .version)" != ""
close_all
ctl eval 'hl.config({ general = { layout = "dwindle" }, plugin = { hyprweave = { auto_join = false } } })' >/dev/null

echo "== column 75/25, locked"
open hw-stream hw-chat
check "create returns true" test "$(lua 'return hl.plugin.hyprweave.create({ direction = "column", lock = "full", children = { { window = "class:^hw-stream$", size = 75 }, { window = "class:^hw-chat$", size = 25 } } })')" = true
sleep 1
check "both members in one group" test "$(field hw-stream '.grouped | length')" -eq 2
# a cold nest may still be animating the first windows in
check "stream gets 75% of the height" wait_for 3 ratio_near hw-stream hw-chat h 75
check "members share x and width" test "$(field hw-stream '.at[0],.size[0]')" = "$(field hw-chat '.at[0],.size[0]')"
check "get() reports lock and direction" test "$(lua 'local g = hl.plugin.hyprweave.get({ window = "class:^hw-stream$" }); return g.lock .. "," .. g.direction')" = "full,column"

for layout in master scrolling dwindle; do
    ctl eval "hl.config({ general = { layout = \"$layout\" } })" >/dev/null
    sleep 1
    check "ratio holds in $layout" near "$(ratio hw-stream hw-chat h)" 75
done

echo "== the frame takes a lone window's border ring; members sit inside it"
top() { field hw-stream '.at[1]'; }
ctl eval 'hl.config({ plugin = { hyprweave = { frame_size = 0 } } })' >/dev/null
sleep 0.5
BARE=$(top)
ctl eval 'hl.config({ plugin = { hyprweave = { frame_size = 2 } } })' >/dev/null
sleep 0.5
GAP=$(ctl getoption general:gaps_in -j | jq -r '.css // .custom' | awk '{print $1}')
check "frame insets members by frame_size + gaps_in" test "$(($(top) - BARE))" -eq "$((2 + GAP))"
check "ratio holds inside the frame" near "$(ratio hw-stream hw-chat h)" 75

echo "== gap between members"
BORDER=$(ctl getoption general:border_size -j | jq .int)
between() { echo $(($(field hw-chat '.at[1]') - $(field hw-stream '.at[1] + .size[1]'))); } # content to content
check "default gap is gaps_in on both sides" test "$(between)" -eq "$((2 * GAP + 2 * BORDER))"
ctl eval 'hl.config({ plugin = { hyprweave = { gap = 0 } } })' >/dev/null
sleep 0.5
check "plugin:hyprweave:gap = 0 puts members border to border" test "$(between)" -eq "$((2 * BORDER))"
ctl eval 'hl.config({ plugin = { hyprweave = { gap = -1 } } })' >/dev/null
sleep 0.5
check "gap = -1 goes back to gaps_in" test "$(between)" -eq "$((2 * GAP + 2 * BORDER))"

echo "== new windows stay out (auto_join = false)"
ctl dispatch 'hl.dsp.focus({ window = "class:^hw-stream$" })' >/dev/null
open hw-other
check "new window not grouped" test "$(field hw-other '.grouped | length')" -eq 0
check "group still has two members" test "$(field hw-stream '.grouped | length')" -eq 2

echo "== frame and background pixels"
# grim reads the nest's own layout coordinates; scale is 1 in every nest config
WL=$("$N" wayland)
pixel() { WAYLAND_DISPLAY=$WL grim -g "$1,$2 1x1" -t ppm - | tail -c 3 | od -An -tx1 | tr -d ' \n'; }
pixel_is() { [[ $(pixel "$1" "$2") == "$3" ]]; } # predicates for wait_for
pixel_not() { [[ $(pixel "$1" "$2") != "$3" ]]; }
ctl eval 'hl.config({ plugin = { hyprweave = { frame_size = 6, gap = 20, frame_color_active = "rgb(ff0000)", frame_color_inactive = "rgb(0000ff)", background_color = "rgb(00ff00)" } } })' >/dev/null
ctl dispatch 'hl.dsp.focus({ window = "class:^hw-stream$" })' >/dev/null
sleep 1
MIDX=$(field hw-stream '.at[0] + (.size[0] / 2 | floor)')
FRAMEY=$(($(field hw-stream '.at[1]') - BORDER - 10 - 3)) # half a gap of padding, then the middle of the frame
SEAMY=$((($(field hw-stream '.at[1] + .size[1]') + $(field hw-chat '.at[1]')) / 2))
check "frame drawn in the active colour" wait_for 3 pixel_is "$MIDX" "$FRAMEY" ff0000
check "background fills the gap between members" pixel_is "$MIDX" "$SEAMY" 00ff00
check "background fills the padding inside the frame" pixel_is "$MIDX" "$((FRAMEY + 6))" 00ff00
ctl dispatch 'hl.dsp.focus({ window = "class:^hw-other$" })' >/dev/null
check "frame turns inactive when focus leaves the group" wait_for 3 pixel_is "$MIDX" "$FRAMEY" 0000ff
ctl eval 'hl.config({ plugin = { hyprweave = { frame_size = 0 } } })' >/dev/null
check "frame_size = 0 removes the frame" wait_for 3 pixel_not "$MIDX" "$FRAMEY" 0000ff
ctl eval 'hl.config({ plugin = { hyprweave = { frame_size = 2, gap = -1, frame_color_active = "rgb(cba6f7)", frame_color_inactive = "rgb(7f6c99)", background_color = "rgba(00000000)" } } })' >/dev/null
sleep 0.5

echo "== the group moves as one unit"
ctl dispatch 'hl.dsp.window.move({ window = "class:^hw-chat$", workspace = 2 })' >/dev/null
sleep 1
check "both members moved to workspace 2" test "$(field hw-stream .workspace.id),$(field hw-chat .workspace.id)" = "2,2"
check "ratio kept after the move" near "$(ratio hw-stream hw-chat h)" 75
ctl dispatch 'hl.dsp.window.move({ window = "class:^hw-chat$", workspace = 1 })' >/dev/null
ctl dispatch 'hl.dsp.focus({ workspace = 1 })' >/dev/null

echo "== fullscreen: a member alone, or the whole group"
MON=$(ctl monitors -j | jq -r '.[0] | "\(.width / .scale | floor),\(.height / .scale | floor)"')
size() { field "$1" '.size | map(tostring) | join(",")'; }
fullscreen() { [[ $(field "$1" .fullscreen) != 0 ]]; }
windowed() { [[ $(field "$1" .fullscreen) == 0 ]]; }
ctl dispatch 'hl.dsp.focus({ window = "class:^hw-chat$" })' >/dev/null
sleep 0.3
ctl dispatch 'hl.dsp.window.fullscreen()' >/dev/null
check "focused member goes fullscreen" wait_for 3 fullscreen hw-chat
check "the other member does not" windowed hw-stream
check "fullscreen member covers the monitor" test "$(size hw-chat)" = "$MON"
ctl dispatch 'hl.dsp.window.fullscreen()' >/dev/null
check "member leaves fullscreen" wait_for 3 windowed hw-chat
sleep 0.5
check "split is back after member fullscreen" near "$(ratio hw-stream hw-chat h)" 75
check "fullscreen() returns true" test "$(lua 'return hl.plugin.hyprweave.fullscreen({ window = "class:^hw-stream$" })')" = true
sleep 0.5
check "group fullscreen keeps the split" near "$(ratio hw-stream hw-chat h)" 75
check "group fullscreen covers the monitor height" test "$(($(field hw-stream '.size[1]') + $(field hw-chat '.size[1]')))" -ge "$((${MON#*,} - 20))"
check "group fullscreen starts at the monitor top" test "$(field hw-stream '.at[1]')" -eq 0
lua 'return hl.plugin.hyprweave.fullscreen({ window = "class:^hw-stream$" })' >/dev/null
check "group leaves fullscreen" wait_for 3 windowed hw-chat
sleep 0.5
check "split is back after group fullscreen" near "$(ratio hw-stream hw-chat h)" 75
check "group no longer starts at the monitor top" test "$(field hw-stream '.at[1]')" -gt 0

echo "== closing a member of a locked group closes the group"
ctl dispatch 'hl.dsp.window.close({ window = "class:^hw-stream$" })' >/dev/null
check "chat closed too" wait_for 5 gone hw-chat
check "no hyprweave groups left" test "$(groups)" -eq 0

echo "== closing a member of an unlocked group dissolves it"
open hw-a hw-b
lua 'return hl.plugin.hyprweave.create({ direction = "row", children = { { window = "class:^hw-a$" }, { window = "class:^hw-b$" } } })' >/dev/null
sleep 0.5
check "row splits 50/50" near "$(ratio hw-a hw-b w)" 50
ctl dispatch 'hl.dsp.window.close({ window = "class:^hw-b$" })' >/dev/null
check "closed member is gone" wait_for 5 gone hw-b
check "sibling stays open" exists hw-a
check "sibling left the group" wait_for 3 ungrouped hw-a
check "no hyprweave groups left" test "$(groups)" -eq 0

echo "== nested containers, with a per-group gap"
open hw-b hw-c
lua 'return hl.plugin.hyprweave.create({ direction = "row", gap = 3, background = "rgba(8c938860)", children = { { window = "class:^hw-a$", size = 50 }, { size = 50, direction = "column", children = { { window = "class:^hw-b$", size = 75 }, { window = "class:^hw-c$", size = 25 } } } } })' >/dev/null
sleep 1
check "left window gets 50% of the width" near "$(ratio hw-a hw-b w)" 50
check "right column splits 75/25" near "$(ratio hw-b hw-c h)" 75
check "column members share x" test "$(field hw-b '.at[0]')" -eq "$(field hw-c '.at[0]')"
check "per-group gap applies" test "$(($(field hw-c '.at[1]') - $(field hw-b '.at[1] + .size[1]')))" -eq "$((3 + 2 * BORDER))"
check "get() reports the per-group gap" test "$(lua 'return hl.plugin.hyprweave.get({ window = "class:^hw-a$" }).gap')" = 3
check "get() reports the per-group background" test "$(lua 'return hl.plugin.hyprweave.get({ window = "class:^hw-a$" }).background')" = "rgba(8c938860)"

echo "== unload turns hyprweave groups back into tab groups"
"$N" unload >/dev/null
sleep 1
check "members still grouped" test "$(field hw-a '.grouped | length')" -eq 3
check "members share one box again" test "$(field hw-b '.at,.size' | tr -d ' \n')" = "$(field hw-c '.at,.size' | tr -d ' \n')"
"$N" reload >/dev/null
close_all

echo "== errors come back as nil, message"
open hw-a hw-b
err() { lua "local ok, msg = $1; return tostring(ok) .. '|' .. tostring(msg)"; }
check "unknown window" test "$(err 'hl.plugin.hyprweave.create({ direction = "row", children = { { window = "class:^hw-a$" }, { window = "class:^hw-nope$" } } })')" = "nil|window not found"
check "bad direction" test "$(err 'hl.plugin.hyprweave.create({ direction = "diagonal", children = { { window = "class:^hw-a$" } } })')" = 'nil|a node needs either window, or direction = "row" or "column"'
check "one window is not a group" test "$(err 'hl.plugin.hyprweave.create({ direction = "row", children = { { window = "class:^hw-a$" } } })' | cut -d'|' -f1)" = nil
check "negative gap" test "$(err 'hl.plugin.hyprweave.create({ direction = "row", gap = -3, children = { { window = "class:^hw-a$" }, { window = "class:^hw-b$" } } })' | cut -d'|' -f1)" = nil
check "bad colour" test "$(err 'hl.plugin.hyprweave.create({ direction = "row", background = "grey", children = { { window = "class:^hw-a$" }, { window = "class:^hw-b$" } } })' | cut -d'|' -f1)" = nil
check "no group was created by the failed calls" test "$(groups)" -eq 0
check "get() on an ungrouped window is nil" test "$(lua 'return tostring(hl.plugin.hyprweave.get({ window = "class:^hw-a$" }))')" = nil
check "dissolve() outside a group fails" test "$(err 'hl.plugin.hyprweave.dissolve({ window = "class:^hw-a$" })')" = "nil|window is not in a hyprweave group"

echo "== lock levels decide who may join (auto_join = true)"
ctl eval 'hl.config({ plugin = { hyprweave = { auto_join = true } } })' >/dev/null
lua 'return hl.plugin.hyprweave.create({ direction = "row", lock = "members", children = { { window = "class:^hw-a$" }, { window = "class:^hw-b$" } } })' >/dev/null
sleep 0.5
check "get() reports the members lock" test "$(lua 'return hl.plugin.hyprweave.get({ window = "class:^hw-a$" }).lock')" = members
check "bad lock level fails" test "$(err 'hl.plugin.hyprweave.lock({ window = "class:^hw-a$", level = "half" })' | cut -d'|' -f1)" = nil
ctl dispatch 'hl.dsp.focus({ window = "class:^hw-a$" })' >/dev/null
open hw-c
check "a members-locked group keeps new windows out" test "$(field hw-c '.grouped | length')" -eq 0
check "lock() changes the level" test "$(lua 'return hl.plugin.hyprweave.lock({ window = "class:^hw-a$", level = "none" })')" = true
ctl dispatch 'hl.dsp.focus({ window = "class:^hw-a$" })' >/dev/null
open hw-d
check "an unlocked group takes the new window" test "$(field hw-d '.grouped | length')" -eq 3
check "the new window joins the root container" test "$(lua 'return #hl.plugin.hyprweave.get({ window = "class:^hw-a$" }).children')" -eq 3
ctl eval 'hl.config({ plugin = { hyprweave = { auto_join = false } } })' >/dev/null

echo "== dissolve() turns the group back into separate windows"
check "dissolve() returns true" test "$(lua 'return hl.plugin.hyprweave.dissolve({ window = "class:^hw-a$" })')" = true
check "members are ungrouped" wait_for 3 ungrouped hw-d
check "no hyprweave groups left" test "$(groups)" -eq 0
close_all

echo "== floating one member floats the group"
open hw-stream hw-chat
lua 'return hl.plugin.hyprweave.create({ direction = "column", children = { { window = "class:^hw-stream$", size = 75 }, { window = "class:^hw-chat$", size = 25 } } })' >/dev/null
sleep 0.5
ctl dispatch 'hl.dsp.window.float({ action = "toggle", window = "class:^hw-chat$" })' >/dev/null
floats() { [[ $(field hw-stream .floating),$(field hw-chat .floating) == true,true ]]; }
check "both members float" wait_for 3 floats
check "ratio holds while floating" wait_for 3 ratio_near hw-stream hw-chat h 75
check "members still share x" test "$(field hw-stream '.at[0]')" -eq "$(field hw-chat '.at[0]')"

echo
if ((FAILURES > 0)); then
    echo "$FAILURES check(s) failed"
    exit 1
fi
echo "all checks passed"
