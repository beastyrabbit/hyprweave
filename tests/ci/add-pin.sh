#!/usr/bin/env bash
# Pins a Hyprland release in hyprpm.toml and pushes the pin to main, after the daily CI run passed
# on it. Does nothing if main already pins that Hyprland commit. .github/workflows/test.yml runs it
# from a full clone of the tested commit with HYPRLAND_VERSION, HYPRLAND_COMMIT and RUN_URL set.
set -euo pipefail

[[ $HYPRLAND_COMMIT =~ ^[0-9a-f]{40}$ ]]
[[ $HYPRLAND_VERSION =~ ^[0-9]+(\.[0-9]+)+$ ]]

# hyprpm builds src/ with the Makefile. If neither changed since the newest release, the pin can
# name the release's commit (the last pin in the release's hyprpm.toml); otherwise only the
# commit this run tested is proven to work.
TESTED=$(git rev-parse HEAD)
TAG=$(git tag --list 'v*' --sort=-v:refname | head -1)
RELEASED=
[[ -z $TAG ]] || RELEASED=$(git show "$TAG:hyprpm.toml" | grep -o '"[0-9a-f]\{40\}"\]' | tail -1 | tr -d '"]')
if [[ -n $RELEASED ]] && git diff --quiet "$RELEASED" "$TESTED" -- src Makefile; then
    PIN=$RELEASED
    NOTE="src/ and the Makefile are unchanged since $TAG, so the pin names $TAG's build."
else
    PIN=$TESTED
    NOTE="src/ or the Makefile changed since ${TAG:-the last release}, so the pin names the tested, unreleased commit."
fi

# main may have moved on while the tests ran; the pin goes on top of it
git fetch -q origin main
git switch -q --detach origin/main
if grep -q "\"$HYPRLAND_COMMIT\"" hyprpm.toml; then
    echo "hyprpm.toml already pins Hyprland $HYPRLAND_VERSION"
    exit 0
fi

awk -v line="    [\"$HYPRLAND_COMMIT\", \"$PIN\"], # $HYPRLAND_VERSION" '
    /^commit_pins = \[/ { pins = 1 }
    pins && /^\]/ { print line; pins = 0 }
    { print }
' hyprpm.toml >hyprpm.toml.new
mv hyprpm.toml.new hyprpm.toml
grep -q "\"$HYPRLAND_COMMIT\"" hyprpm.toml

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git commit -q -am "release: pin Hyprland $HYPRLAND_VERSION" -m "\
The daily test run passed on Hyprland $HYPRLAND_VERSION (commit $HYPRLAND_COMMIT): $RUN_URL

$NOTE Pinned by tests/ci/add-pin.sh."
git push -q origin HEAD:main
echo "pinned Hyprland $HYPRLAND_VERSION ($HYPRLAND_COMMIT) to $PIN on main"
