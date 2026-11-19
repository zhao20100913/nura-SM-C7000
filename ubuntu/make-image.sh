#!/bin/bash
# Turn a populated root filesystem directory into an ext4 image that can be
# flashed into the USERDATA partition of the Galaxy C7.
#
# The image is created at SIZE_GB, populated, checked and then shrunk to its
# minimum size: the postmarketOS initramfs in boot.img grows both the partition
# and the filesystem to the full size of USERDATA on the first boot
# (resize_root_partition / resize_root_filesystem), so shipping a minimal image
# keeps the heimdall flash short.
set -euo pipefail

ROOT=${1:?usage: make-image.sh <rootdir> <image> [size-gb]}
OUT=${2:?usage: make-image.sh <rootdir> <image> [size-gb]}
SIZE_GB=${3:-3}
LABEL=${LABEL:-ubuntu-root}

ROOT=$(readlink -m "$ROOT")
OUT=$(readlink -m "$OUT")
# Fixed on purpose: build.yml pins the pmOS root filesystem to this UUID so
# that any boot.img this repository produces boots any rootfs it produces.
# Keeping the same value here means the boot image we ship does not even need
# its command line patched to boot the Ubuntu image.
UUID=${UUID:-6fb8f25b-07b4-4934-ab41-0921b7bc6a6c}

[ -d "$ROOT/etc" ] || { echo "$ROOT does not look like a root filesystem" >&2; exit 1; }

# The initramfs only waits for a boot partition when /etc/fstab is empty, and
# systemd wants the real root UUID here.
cat > "$ROOT/etc/fstab" <<EOF
# <file system>                            <mount point>  <type>  <options>                     <dump> <pass>
UUID=$UUID                                 /              ext4    errors=remount-ro,noatime    0      1
tmpfs                                      /tmp           tmpfs   nosuid,nodev,mode=1777        0      0
EOF
cat "$ROOT/etc/fstab"

echo "==> creating ${SIZE_GB}G ext4 image"
rm -f "$OUT"
truncate -s "${SIZE_GB}G" "$OUT"
mkfs.ext4 -q -F -L "$LABEL" -U "$UUID" -m 0 -E root_owner=0:0 "$OUT"

MNT=$(mktemp -d)
cleanup() { mountpoint -q "$MNT" && umount "$MNT" || true; rmdir "$MNT" 2>/dev/null || true; }
trap cleanup EXIT

mount -o loop "$OUT" "$MNT"

echo "==> copying root filesystem"
rsync -aHAX --numeric-ids --info=progress2 \
	--exclude='/proc/*' --exclude='/sys/*' --exclude='/dev/*' \
	--exclude='/run/*' --exclude='/tmp/*' --exclude='/var/cache/apt/*' \
	"$ROOT"/ "$MNT"/

umount "$MNT"

echo "==> checking filesystem"
e2fsck -fy "$OUT" >/dev/null
tune2fs -c 0 -i 0 -L "$LABEL" -U "$UUID" "$OUT" >/dev/null

echo "==> shrinking to the minimum size"
if resize2fs -M "$OUT">/dev/null 2>&1; then
	e2fsck -fy "$OUT" >/dev/null
else
	echo "warning: could not shrink the image, keeping ${SIZE_GB}G" >&2
fi

printf '%s\n' "$UUID" > "${OUT}.uuid"
echo "==> $OUT: $(du -h --apparent-size "$OUT" | cut -f1), UUID $UUID, label $LABEL"
