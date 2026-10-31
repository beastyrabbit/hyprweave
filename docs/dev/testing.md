# Testing

hyprweave is tested end to end: a real Hyprland runs, the plugin loads into it, test windows open, and the checks read geometry, state and pixels back through `hyprctl` and `grim`. There are no unit tests; the layout maths in `Tree.cpp` is covered through the geometry checks.

## The suite

`tests/nest-test.sh` needs a running nest and kitty. It loads the current build, opens kitty windows with classes starting with `hw-`, runs its checks and closes those windows again (also on failure). It takes about 25 seconds.

```sh
dev/nest start          # in a terminal inside Hyprland; keeps running
tests/nest-test.sh      # in a second terminal
```

It covers:

| Section | Checks |
| --- | --- |
| Column 75/25, locked | `create`, the split, `get()`, the split in dwindle, master and scrolling |
| Frame and gap | the frame's inset, `gap` from the config (`-1`, `0`), the per-group `gap` |
| Frame and background pixels | frame colour while focused and unfocused, background in the gap and the padding, `frame_size = 0` |
| Joining | new windows stay out with `auto_join = false`; with `auto_join = true` a `members` lock keeps them out and `none` lets them in |
| Moving and floating | the group changes workspace as one; floating one member floats the group with its split |
| Fullscreen | a member alone, the whole group with `fullscreen()`, and the split coming back |
| Closing | a locked group closes with its member; an unlocked one dissolves when one window is left |
| Nesting | a row with a column inside it |
| API errors | each bad input returns `nil, message` and creates nothing; `dissolve()` |
| Unload | groups turn back into tab groups |

Add a check for every new behaviour. Helpers at the top of the script:

- `check "description" <command...>` passes when the command succeeds.
- `wait_for <seconds> <predicate...>` retries a predicate every 0.25 s. Use it for anything that animates or happens on the next loop iteration. Predicates must query state themselves; `wait_for 3 test "$(field ...)" = x` only evaluates the query once.
- `field <class> <jq filter>` reads one window from `hyprctl clients -j`; `ratio`, `near` and `ratio_near` compare sizes.
- `pixel <x> <y>` returns the colour at a layout coordinate as `rrggbb`.

## CI

[`.github/workflows/test.yml`](../../.github/workflows/test.yml) runs the same suite on every push to `main`, on tags, on pull requests from branches of this repo, daily at 04:17 UTC, and on demand. The daily run builds against whatever Hyprland Arch Linux ships that day, so it is the first warning that a new Hyprland release breaks the plugin. The job summary shows the Hyprland version it tested.

It runs on the self-hosted runner `arc-hyprweave` (Actions Runner Controller in the maintainer's cluster). Pull requests from forks never run there: the job skips them, and because a fork can edit that condition, the repository also requires a maintainer's approval before any outside contributor's workflow runs (Settings, Actions, "Require approval for all external contributors"). Do not approve a fork's run; a maintainer runs a fork's change by pushing the branch to this repo.

Hyprland needs a DRM device even for headless outputs, and CI has no GPU. So [`tests/ci/run.sh`](../../tests/ci/run.sh) runs in an `archlinux:latest` container with `/dev/kvm`, installs Hyprland, kitty and a kernel, builds the plugin, and boots a small VM with [virtme-ng](https://github.com/arighi/virtme-ng). The VM uses the container's own filesystem and has a virtual GPU (`virtio-gpu-pci`, 1920x1080); Mesa renders in software (`LIBGL_ALWAYS_SOFTWARE=1`). Inside the VM, [`tests/ci/in-vm.sh`](../../tests/ci/in-vm.sh) starts `seatd`, then `dev/nest start --bare` and the suite as an ordinary user.

To run CI locally (about 4 minutes, most of it package downloads):

```sh
docker run --rm --device /dev/kvm -v "$PWD:/src" -w /src archlinux:latest tests/ci/run.sh
```

This writes `hyprweave.so` and `ci-logs/` as root into the checkout; run it on a copy if that matters. On failure the logs (`hyprland.log`, the suite's output, package versions) are in `ci-logs/`, and the workflow uploads them as an artifact.
