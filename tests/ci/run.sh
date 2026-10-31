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
#
# HYPRLAND_GIT=1 tests Hyprland's main branch instead of its newest release, using chaotic-aur's
# hyprland-git; CHAOTIC_MIRROR (a pacman Server URL) goes before chaotic-aur's own server.
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
COMMAND=${1:-tests/nest-test.sh}
LOGS="$ROOT/ci-logs"
mkdir -p "$LOGS"

echo "== install"
# PACMAN_MIRROR (a pacman Server URL, e.g. a caching proxy) goes before the normal mirrors,
# which stay as the fallback
if [[ -n ${PACMAN_MIRROR:-} ]]; then
    { echo "Server = $PACMAN_MIRROR"; cat /etc/pacman.d/mirrorlist; } >/tmp/mirrorlist
    mv /tmp/mirrorlist /etc/pacman.d/mirrorlist
fi
HYPRLAND=hyprland
if [[ -n ${HYPRLAND_GIT:-} ]]; then
    # chaotic-aur's signing key, by full fingerprint
    CHAOTIC_KEY=EF925EA60F33D0CB85C44AD13056513887B78AEB # gitleaks:allow (public key fingerprint)
    # the image has Arch's keys but no local key to sign chaotic-aur's with
    pacman-key --init >"$LOGS/chaotic-key.log" 2>&1
    # over HTTPS: the CI runner can't reach the keyserver port (11371). Signing by full
    # fingerprint fails if that key is missing from the file; other keys in it stay unsigned.
    curl -fsS --retry 3 "https://keyserver.ubuntu.com/pks/lookup?op=get&options=mr&search=0x$CHAOTIC_KEY" \
        -o /tmp/chaotic.asc
    pacman-key --add /tmp/chaotic.asc >>"$LOGS/chaotic-key.log" 2>&1
    pacman-key --lsign-key "$CHAOTIC_KEY" >>"$LOGS/chaotic-key.log" 2>&1
    {
        printf '\n[chaotic-aur]\n'
        [[ -z ${CHAOTIC_MIRROR:-} ]] || echo "Server = $CHAOTIC_MIRROR"
        echo 'Server = https://builds.garudalinux.org/repos/chaotic-aur/$arch'
    } >>/etc/pacman.conf
    # the keys of chaotic-aur's other builders, signed by the key above
    pacman -Sy --noconfirm --noprogressbar chaotic-keyring >>"$LOGS/chaotic-key.log"
    HYPRLAND=hyprland-git
fi
pacman -Syu --noconfirm --noprogressbar --needed \
    "$HYPRLAND" gcc make pkgconf \
    linux virtme-ng qemu-base qemu-hw-display-virtio-gpu qemu-hw-display-virtio-gpu-pci seatd \
    kitty ttf-dejavu grim jq ${EXTRA_PACKAGES:-} >"$LOGS/pacman.log"
pacman -Q "$HYPRLAND" mesa linux | tee "$LOGS/versions.txt"
# the first line names the Hyprland commit, which a hyprpm.toml pin needs
Hyprland --version | head -1 | tee -a "$LOGS/versions.txt"

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
