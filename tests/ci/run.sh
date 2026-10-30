#!/usr/bin/env bash
# Runs tests/nest-test.sh against the newest Hyprland in Arch Linux, with no GPU and no display.
# Hyprland needs a DRM device, so the tests run in a small VM (virtme-ng) with a virtual GPU
# and software rendering. Needs root in an Arch Linux container started with /dev/kvm:
#
#   docker run --rm --device /dev/kvm -v "$PWD:/src" -w /src archlinux:latest tests/ci/run.sh
#
# Logs land in ./ci-logs. Pass a script to run it in the VM instead of the suite (with Hyprland
# and the plugin running, as an ordinary user); EXTRA_PACKAGES adds packages for it:
#
#   EXTRA_PACKAGES="mpv wf-recorder" tests/ci/run.sh dev/media/record.sh
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
COMMAND=${1:-tests/nest-test.sh}
LOGS="$ROOT/ci-logs"
mkdir -p "$LOGS"

echo "== install"
pacman -Syu --noconfirm --noprogressbar --needed \
    hyprland gcc make pkgconf \
    linux virtme-ng qemu-base qemu-hw-display-virtio-gpu qemu-hw-display-virtio-gpu-pci seatd \
    kitty ttf-dejavu grim jq ${EXTRA_PACKAGES:-} >"$LOGS/pacman.log"
pacman -Q hyprland aquamarine mesa linux | tee "$LOGS/versions.txt"

echo "== build"
make -C "$ROOT"

# Hyprland refuses to run as root
id tester >/dev/null 2>&1 || useradd -m tester
chmod -R a+rwX "$LOGS"

echo "== test in a VM"
KERNEL=$(ls /usr/lib/modules/*/vmlinuz | head -1)
vng --run "$KERNEL" --disable-microvm --force-9p --memory "${VM_MEMORY:-2G}" --cpus "${VM_CPUS:-4}" \
    --rwdir "$LOGS" \
    --qemu-opts="-device virtio-gpu-pci,xres=1920,yres=1080" \
    --exec "$ROOT/tests/ci/in-vm.sh $ROOT/$COMMAND"

# the VM writes the command's exit status; a VM that never got that far is a failure too
STATUS=$(cat "$LOGS/status" 2>/dev/null || echo "missing")
echo "== $COMMAND exit status: $STATUS"
[[ $STATUS == 0 ]]
