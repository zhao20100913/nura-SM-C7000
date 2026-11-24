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
LABEL=${LABEL:-debian-root}

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

# Size the filesystem just above the payload: resize2fs -M can only give back
# the blocks after the last allocated one, so a 3 GiB image with 1.7 GiB of
# files stays at 3 GiB.  The initramfs grows everything to USERDATA anyway.
DATA_MB=$(du -sm --apparent-size "$ROOT" | cut -f1)
NEED_MB=$(( DATA_MB + DATA_MB / 10 + 400 ))
REQ_MB=$(( SIZE_GB * 1024 ))
IMG_MB=$NEED_MB
[ "$IMG_MB" -gt "$REQ_MB" ] && IMG_MB=$REQ_MB
[ "$IMG_MB" -lt 1024 ] && IMG_MB=1024
echo "==> creating ${IMG_MB}M ext4 image (payload ${DATA_MB}M, limit ${REQ_MB}M)"
rm -f "$OUT"
truncate -s "${IMG_MB}M" "$OUT"
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
if resize2fs -M "$OUT" >/dev/null 2>&1; then
	e2fsck -fy "$OUT" >/dev/null
	BLOCKS=$(dumpe2fs -h "$OUT" 2>/dev/null | awk '/^Block count:/{print $3}')
	BSIZE=$(dumpe2fs -h "$OUT" 2>/dev/null | awk '/^Block size:/{print $3}')
	if [ -n "$BLOCKS" ] && [ -n "$BSIZE" ]; then
		truncate -s $(( BLOCKS * BSIZE )) "$OUT"
	fi
else
	echo "warning: could not shrink the image, keeping ${IMG_MB}M" >&2
fi

printf '%s\n' "$UUID" > "${OUT}.uuid"
echo "==> $OUT: $(du -h --apparent-size "$OUT" | cut -f1), UUID $UUID, label $LABEL"
