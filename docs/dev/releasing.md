# Releasing

## Versions

hyprweave uses semantic versions. The version lives in `HYPRWEAVE_VERSION` (`src/globals.hpp`); `hyprctl plugin list` and `hyprctl hyprweave` show it. Tags are `vX.Y.Z`. While the API can still change, releases are marked as pre-releases on GitHub.

## Making a release

1. Bump `HYPRWEAVE_VERSION`.
2. Run `tests/nest-test.sh` and make sure the CI run for the commit is green.
3. Commit, then add a pin to `hyprpm.toml` for every Hyprland release the commit supports: `["<Hyprland commit>", "<hyprweave commit>"], # 0.57.0`. `Hyprland --version` prints the Hyprland commit. Commit the pin separately, because it names the commit before it.
4. Tag the pin commit `vX.Y.Z`, push `main` and the tag, and publish the GitHub release from the tag.

## After a Hyprland release

The daily CI run builds against the newest Hyprland in Arch Linux. When it fails after a Hyprland update:

1. Update Hyprland locally, rebuild, start a nest and run the suite.
2. If the plugin refuses to load, a hook target changed: compare each signature in [basics/hooks.md](basics/hooks.md) with the new source and fix the matches in `src/Weave.cpp`.
3. Re-check [basics/groups.md](basics/groups.md) against the new group code; the tree sync relies on it.
4. Release, and pin the new Hyprland commit to the new hyprweave commit. Keep the old pins: users on the older Hyprland keep getting the build that works for them.

## README media

The screenshots and videos in `docs/media` come from the CI VM, so they show a clean 1920x1080 session with demo windows only. To record them again after a visible change:

```sh
docker run --rm --device /dev/kvm -v "$PWD:/src" -w /src \
    -e EXTRA_PACKAGES="mpv wf-recorder bat btop ttf-jetbrains-mono" -e VM_CPUS=8 -e VM_MEMORY=6G \
    archlinux:latest tests/ci/run.sh dev/media/record.sh
dev/media/encode.sh          # ci-logs -> docs/media (needs ffmpeg)
```

`dev/media/record.sh` holds the scenes; the fake chat and the Lua snippet shown in them are next to it.
