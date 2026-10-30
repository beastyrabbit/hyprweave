#!/usr/bin/env bash
# A fake chat for the README recordings: prints a line every second or so.
NAMES=(mira ollie kenji sam mira ollie kenji ana sam mira ollie ana)
TEXTS=(
    "that transition was clean"
    "is this the new layout?"
    "stream on top, chat below, finally"
    "how do you keep them together"
    "it's one group, moves as one window"
    "and fullscreen still works?"
    "yep, the video alone"
    "nice frame colour"
    "link to the plugin pls"
    "github.com/beastyrabbit/hyprweave"
    "ok installing that tonight"
    "gg"
)
COLORS=(35 36 33 32 34 31)
i=0
while true; do
    n=$((i % ${#TEXTS[@]}))
    printf '\e[2m%s\e[0m \e[1;%sm%s\e[0m  %s\n' "$(date +%H:%M)" "${COLORS[i % ${#COLORS[@]}]}" "${NAMES[n]}" "${TEXTS[n]}"
    i=$((i + 1))
    sleep 0.9
done
