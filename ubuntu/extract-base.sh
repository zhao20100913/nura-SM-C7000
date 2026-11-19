#!/bin/bash
# Extract the pieces of the pmOS/Nura image that the Ubuntu rootfs needs but
# cannot grow itself: the kernel modules matching the kernel inside boot.img,
# the non-free firmware (Adreno a530, WCNSS wifi calibration) and the
# msm-firmware-loader helpers (firmware that lives on the Android partitions of
# the device, e.g. wcnss.mdt on apnhlos).
set -euo pipefail

BASE=${1:?usage: extract-base.sh <base-dir> <out-dir>}
OUT=${2:?usage: extract-base.sh <base-dir> <out-dir>}

ROOTIMG=$(ls "$BASE"/*-root.img 2>/dev/null | head -n1 || true)
[ -n "$ROOTIMG" ] || { echo "no *-root.img in $BASE" >&2; exit 1; }

MNT=$(mktemp -d)
cleanup() { mountpoint -q "$MNT" && umount "$MNT" || true; rmdir "$MNT" 2>/dev/null || true; }
trap cleanup EXIT

mount -o loop,ro "$ROOTIMG" "$MNT"
echo "mounted $ROOTIMG"

KREL=$(ls "$MNT/lib/modules" | head -n1)
[ -n "$KREL" ] || { echo "no modules in $ROOTIMG" >&2; exit 1; }
echo "kernel release: $KREL"

mkdir -p "$OUT/lib/modules" "$OUT/lib/firmware" "$OUT/usr/bin"
cp -a "$MNT/lib/modules/$KREL" "$OUT/lib/modules/"
echo "modules: $(du -sh "$OUT/lib/modules/$KREL" | cut -f1)"

if [ -d "$MNT/usr/lib/firmware" ]; then
	cp -a "$MNT/usr/lib/firmware/." "$OUT/lib/firmware/"
fi
if [ -d "$MNT/lib/firmware" ] && [ ! -L "$MNT/lib/firmware" ]; then
	cp -an "$MNT/lib/firmware/." "$OUT/lib/firmware/" 2>/dev/null || true
fi
echo "firmware: $(du -sh "$OUT/lib/firmware" | cut -f1)"

cp -a "$MNT"/usr/bin/msm-firmware-loader*.sh "$OUT/usr/bin/" 2>/dev/null || true
printf '%s\n' "$KREL" > "$OUT/kernel-release"
ls -l "$OUT/usr/bin" || true
