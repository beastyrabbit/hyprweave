#!/usr/bin/env bash
# Opens a pull request that pins a Hyprland release in hyprpm.toml, after the daily CI run passed
# on it. Does nothing if hyprpm.toml already pins that Hyprland commit or the pull request is
# already open. .github/workflows/test.yml runs it from a full clone of the tested commit with
# GH_TOKEN, HYPRLAND_VERSION, HYPRLAND_COMMIT and RUN_URL set.
set -euo pipefail

[[ $HYPRLAND_COMMIT =~ ^[0-9a-f]{40}$ ]]
[[ $HYPRLAND_VERSION =~ ^[0-9]+(\.[0-9]+)+$ ]]

if grep -q "\"$HYPRLAND_COMMIT\"" hyprpm.toml; then
    echo "hyprpm.toml already pins Hyprland $HYPRLAND_VERSION"
    exit 0
fi
BRANCH="pin/hyprland-$HYPRLAND_VERSION"
if [[ -n $(gh pr list --head "$BRANCH" --state open --json number --jq '.[].number') ]]; then
    echo "a pull request for $BRANCH is already open"
    exit 0
fi

# hyprpm builds src/ with the Makefile. If neither changed since the newest release, the pin can
# name the release's commit (the last pin in the release's hyprpm.toml); otherwise only the
# commit this run tested is proven to work.
TESTED=$(git rev-parse HEAD)
TAG=$(git tag --list 'v*' --sort=-v:refname | head -1)
RELEASED=
[[ -z $TAG ]] || RELEASED=$(git show "$TAG:hyprpm.toml" | grep -o '"[0-9a-f]\{40\}"\]' | tail -1 | tr -d '"]')
if [[ -n $RELEASED ]] && git diff --quiet "$RELEASED" "$TESTED" -- src Makefile; then
    PIN=$RELEASED
    NOTE="hyprweave's source is unchanged since $TAG, so the pin names $TAG's commit, \`$PIN\`."
else
    PIN=$TESTED
    NOTE="hyprweave's source changed since ${TAG:-the last release}, so the pin names the commit this run tested, \`$PIN\`, which is not released. Consider releasing it first and pointing the pin at the release."
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
git switch -c "$BRANCH"
git commit -q -am "release: pin Hyprland $HYPRLAND_VERSION"
# a branch left from an earlier, closed pull request is replaced
git push -q --force origin "$BRANCH"
gh pr create --base main --head "$BRANCH" --title "Pin Hyprland $HYPRLAND_VERSION" --body "\
The daily test run passed on Hyprland $HYPRLAND_VERSION (commit \`$HYPRLAND_COMMIT\`), which \`hyprpm.toml\` does not pin yet: $RUN_URL

$NOTE Merging this makes \`hyprpm update\` build that commit for users on Hyprland $HYPRLAND_VERSION.

Opened by the test workflow (\`tests/ci/propose-pin.sh\`)."
