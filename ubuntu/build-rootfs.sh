#!/bin/bash
# Build a Ubuntu 26.04 LTS (resolute) arm64 root filesystem for the Samsung
# Galaxy C7 (SM-C7000 / c7lte) out of a debootstrap chroot.
#
# The kernel, initramfs and device tree stay untouched: they come from the
# repository's own Nura CI image (see fetch-base.py / extract-base.sh).  This
# script only creates the user space around them:
#   * debootstrap + apt install of the package list in packages.txt
#   * kernel modules and firmware taken from the pmOS root filesystem
#   * Chinese locale/timezone, user c7, ssh over USB/NetworkManager, Phosh
set -euo pipefail

ROOT=${1:?usage: build-rootfs.sh <rootdir> <base-dir> [packages.txt]}
BASE=${2:?usage: build-rootfs.sh <rootdir> <base-dir> [packages.txt]}
HERE=$(cd "$(dirname "$0")" && pwd)
PKG_LIST=${3:-$HERE/packages.txt}

ROOT=$(readlink -m "$ROOT")
BASE=$(readlink -m "$BASE")
MIRROR=${MIRROR:-http://ports.ubuntu.com/ubuntu-ports}
RELEASE=${RELEASE:-resolute}
QEMU=${QEMU:-/usr/bin/qemu-aarch64-static}
USERNAME=${USERNAME:-c7}
PASSWORD=${PASSWORD:-147147}

log() { printf '\n==> %s\n' "$*"; }

[ -x "$QEMU" ] || { echo "$QEMU not found (install qemu-user-static)" >&2; exit 1; }

# debootstrap 1.0.134 (the version in noble and on the runners) predates
# 26.04 and has no per-suite script for it.  These scripts are generic -- the
# suite name is only used for the mirror lookup and a couple of legacy checks
# -- so the noble one works unchanged for resolute.
DBSCRIPTS=/usr/share/debootstrap/scripts
if [ ! -f "$DBSCRIPTS/$RELEASE" ] && [ -f "$DBSCRIPTS/noble" ]; then
	if [ "$(id -u)" -ne 0 ]; then SUDO=sudo; else SUDO=; fi
	$SUDO install -m 0644 "$DBSCRIPTS/noble" "$DBSCRIPTS/$RELEASE"
fi

cleanup() {
	for d in dev/pts dev/shm dev proc sys; do
		mountpoint -q "$ROOT/$d" && umount -R "$ROOT/$d" || true
	done
}
trap cleanup EXIT

# ---------------------------------------------------------------- debootstrap
log "debootstrap $RELEASE arm64 from $MIRROR"
if [ -f "$ROOT/etc/debian_version" ] && [ "${FRESH:-0}" != 1 ]; then
	log "reusing the existing root filesystem in $ROOT"
else
rm -rf "$ROOT"
mkdir -p "$ROOT"
for attempt in 1 2 3; do
	if debootstrap --arch=arm64 --variant=minbase --components=main,universe \
		--include=ca-certificates,apt-utils,gnupg "$RELEASE" "$ROOT" "$MIRROR"; then
		break
	fi
	echo "debootstrap attempt $attempt failed, retrying in $((attempt * 10))s"
	sleep $((attempt * 10))
done
[ -d "$ROOT/etc" ] || { echo "debootstrap failed" >&2; exit 1; }
fi

cat > "$ROOT/etc/apt/sources.list" <<EOF
deb $MIRROR $RELEASE main universe
deb $MIRROR $RELEASE-updates main universe
deb $MIRROR $RELEASE-security main universe
EOF

cp "$QEMU" "$ROOT/usr/bin/qemu-aarch64-static"

# -------------------------------------------------------------------- chroot
mount --bind /dev "$ROOT/dev"
mount -t devpts devpts "$ROOT/dev/pts"
mount -t tmpfs tmpfs "$ROOT/dev/shm"
mount -t proc proc "$ROOT/proc"
mount -t sysfs sysfs "$ROOT/sys"

in_root() {
	chroot "$ROOT" env -i \
		PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
		HOME=/root TERM=dumb DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8 \
		"$@"
}

# ---------------------------------------------------------------- packages
log "apt-get update"
in_root apt-get update

# Drop package names that this mirror/release does not know instead of failing
# the whole build over a typo or a package that moved to another component.
mapfile -t WANTED < <(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' -e 's/[[:space:]]//g' "$PKG_LIST")
INSTALL=()
MISSING=()
for pkg in "${WANTED[@]}"; do
	if in_root apt-cache show "$pkg" >/dev/null 2>&1; then
		INSTALL+=("$pkg")
	else
		MISSING+=("$pkg")
	fi
done
if [ ${#MISSING[@]} -gt 0 ]; then
	log "warning: not available in $RELEASE: ${MISSING[*]}"
fi
if [ -f "$ROOT/.c7-packages-installed" ] && [ "${FRESH:-0}" != 1 ]; then
	log "packages already installed, skipping"
else
	log "installing ${#INSTALL[@]} packages"
	in_root apt-get install -y --no-install-recommends "${INSTALL[@]}"
	touch "$ROOT/.c7-packages-installed"
fi

# ------------------------------------------------------------ DenialWM
DESKTOP=${DESKTOP:-phosh}
if [ "$DESKTOP" = "denial" ]; then
	log "installing DenialWM (glibc arm64 debs)"
	mkdir -p "$ROOT/tmp/denial"
	for deb in denial_0.6.8-1_arm64.deb \
		   denial-flutter-engine_0.6.8-1_arm64.deb; do
		curl -fL --retry 3 --retry-delay 5 \
			-o "$ROOT/tmp/denial/$deb" \
			"https://github.com/denialwm/denial/releases/download/v0.6.8/$deb"
	done
	chmod 644 "$ROOT/tmp/denial/"*.deb

	DENIAL_DEPS="libseat1 libinput10 libxkbcommon0 libgbm1 libegl1 libgles2
		libvulkan1 libpulse0 pipewire wireplumber xdg-desktop-portal
		xdg-desktop-portal-wlr xwayland fonts-noto-cjk
		fonts-noto-color-emoji dbus-x11 polkitd rtkit"
	DENIAL_INSTALL=()
	for pkg in $DENIAL_DEPS; do
		if in_root apt-cache show "$pkg" >/dev/null 2>&1; then
			DENIAL_INSTALL+=("$pkg")
		else
			log "warning: Denial dependency $pkg not in $RELEASE"
		fi
	done
	if [ ${#DENIAL_INSTALL[@]} -gt 0 ]; then
		in_root apt-get install -y --no-install-recommends \
			"${DENIAL_INSTALL[@]}"
	fi
	in_root apt-get install -y \
		/tmp/denial/denial-flutter-engine_0.6.8-1_arm64.deb \
		/tmp/denial/denial_0.6.8-1_arm64.deb
	rm -rf "$ROOT/tmp/denial"
fi

# Keep the image small: no documentation, no apt cache, no man pages.
in_root apt-get clean
rm -rf "$ROOT/var/lib/apt/lists"/* "$ROOT/var/cache/apt/archives"/*.deb
rm -rf "$ROOT/usr/share/doc"/* "$ROOT/usr/share/man"/* "$ROOT/usr/share/info"/*

# -------------------------------------------------------------- locales/time
log "locales + timezone"
cat > "$ROOT/etc/locale.gen" <<EOF
en_US.UTF-8 UTF-8
zh_CN.UTF-8 UTF-8
EOF
in_root locale-gen
cat > "$ROOT/etc/default/locale" <<EOF
LANG=zh_CN.UTF-8
LANGUAGE=zh_CN:zh:en_US:en
LC_TIME=zh_CN.UTF-8
LC_NUMERIC=zh_CN.UTF-8
LC_MONETARY=zh_CN.UTF-8
LC_PAPER=zh_CN.UTF-8
LC_NAME=zh_CN.UTF-8
LC_ADDRESS=zh_CN.UTF-8
LC_TELEPHONE=zh_CN.UTF-8
LC_MEASUREMENT=zh_CN.UTF-8
LC_IDENTIFICATION=zh_CN.UTF-8
EOF
ln -sf /usr/share/zoneinfo/Asia/Shanghai "$ROOT/etc/localtime"
printf 'Asia/Shanghai\n' > "$ROOT/etc/timezone"
in_root dpkg-reconfigure -f noninteractive tzdata || true

# --------------------------------------------------------------------- user
log "creating user $USERNAME"
for grp in sudo adm video audio input plugdev netdev dialout; do
	in_root groupadd -f "$grp"
done
if ! in_root id -u "$USERNAME" >/dev/null 2>&1; then
	in_root useradd -m -s /bin/bash -G sudo,adm,video,audio,input,plugdev,netdev,dialout "$USERNAME"
fi
printf '%s:%s\n' "$USERNAME" "$PASSWORD" | in_root chpasswd
# root login is only possible through sudo (no root password)
in_root passwd -l root || true
cat >> "$ROOT/etc/sudoers.d/90-$USERNAME" <<EOF
$USERNAME ALL=(ALL) NOPASSWD: ALL
EOF
chmod 0440 "$ROOT/etc/sudoers.d/90-$USERNAME"

# ------------------------------------------------------------------- overlay
log "installing overlay files"
cp -a "$HERE/overlay/." "$ROOT"/
# The user only exists inside the chroot, so resolve it there and chown by id.
UID_=$(in_root id -u "$USERNAME")
GID_=$(in_root id -g "$USERNAME")
chown -R "$UID_:$GID_" "$ROOT/home/$USERNAME"
chmod 0755 "$ROOT/usr/local/bin/c7-start-phosh" \
	"$ROOT/usr/local/bin/c7-start-denial" \
	"$ROOT/usr/local/sbin/c7-usb-gadget.sh"
chmod 0600 "$ROOT/etc/NetworkManager/system-connections/usb0.nmconnection"

# ---------------------------------------------------- kernel modules/firmware
log "installing kernel modules and firmware from $BASE"
KREL=$(cat "$BASE/kernel-release")
mkdir -p "$ROOT/lib/modules"
cp -a "$BASE/lib/modules/$KREL" "$ROOT/lib/modules/"
mkdir -p "$ROOT/lib/firmware"
cp -a "$BASE/lib/firmware/." "$ROOT/lib/firmware/"
in_root depmod -a "$KREL"

# ------------------------------------------------------------------ services
log "enabling services"
for unit in \
	ssh.service \
	NetworkManager.service \
	bluetooth.service \
	msm-firmware-loader.service \
	c7-wifi.service \
	c7-usb-gadget.service \
	c7-usb-dhcpd.service \
	getty@tty1.service \
	serial-getty@ttyMSM0.service \
	serial-getty@ttyGS0.service \
	; do
	if in_root systemctl enable "$unit" >/dev/null 2>&1; then
		echo "  enabled $unit"
	else
		echo "  warning: could not enable $unit"
	fi
done

# ------------------------------------------------------------------- cleanup
log "cleaning up"
rm -f "$ROOT/usr/bin/qemu-aarch64-static"
rm -f "$ROOT"/etc/ssh/ssh_host_*
: > "$ROOT/etc/machine-id"
rm -rf "$ROOT/tmp"/* "$ROOT/var/tmp"/* "$ROOT/var/log"/* 2>/dev/null || true
find "$ROOT/var/log" -type f -exec truncate -s 0 {} + 2>/dev/null || true

log "root fs size: $(du -sh --apparent-size "$ROOT" | cut -f1)"
log "done"
