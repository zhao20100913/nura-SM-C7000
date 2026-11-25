#!/bin/bash
# Build a Fedora 43 (aarch64) + Phosh root filesystem for the Galaxy C7.
# The kernel/initramfs/firmware come from the pmOS base image; this script
# only creates the user space and the device integration files.
set -euo pipefail

ROOT=${1:?usage: build-rootfs.sh <rootdir> <base-dir> [packages.txt]}
BASE=${2:?usage: build-rootfs.sh <rootdir> <base-dir> [packages.txt]}
HERE=$(cd "$(dirname "$0")" && pwd)
PKG_LIST=${3:-$HERE/packages.txt}

RELEASE=43
IMAGES="https://dl.fedoraproject.org/pub/fedora/linux/releases/$RELEASE/Container/aarch64/images/"
ROOTFS_TAR=/tmp/fedora-wsl-$RELEASE.tar

log() { printf '\n==> %s\n' "$*"; }

# ---------------------------------------------------------------- rootfs tar
if [ ! -s "$ROOTFS_TAR" ] || [ "${FRESH:-0}" = 1 ]; then
	log "fetching Fedora $RELEASE aarch64 WSL rootfs"
	FILE=$(curl -fsSL "$IMAGES" |
		grep -oE 'Fedora-WSL-Base-[0-9.]+-[0-9.]+\.aarch64\.wsl' |
		sort -u | tail -n1)
	[ -n "$FILE" ] || { echo "no Fedora WSL image in $IMAGES" >&2; exit 1; }
	echo "image: $FILE"
	curl -fL --retry 3 -o "$ROOTFS_TAR" "$IMAGES$FILE"
fi

rm -rf "$ROOT"
mkdir -p "$ROOT"
tar -xf "$ROOTFS_TAR" -C "$ROOT"
[ -f "$ROOT/etc/os-release" ] || { echo "not a rootfs" >&2; exit 1; }
rm -f "$ROOT/etc/resolv.conf"
echo 'nameserver 223.5.5.5' > "$ROOT/etc/resolv.conf"

QEMU=$(command -v qemu-aarch64-static || true)
[ -n "$QEMU" ] || { echo "qemu-aarch64-static missing" >&2; exit 1; }
cp "$QEMU" "$ROOT/usr/bin/qemu-aarch64-static"

mkdir -p "$ROOT/dev" "$ROOT/dev/pts" "$ROOT/dev/shm" "$ROOT/proc" "$ROOT/sys" \
	"$ROOT/run" "$ROOT/tmp" "$ROOT/var/tmp"
mount --bind /dev "$ROOT/dev"
mount -t devpts devpts "$ROOT/dev/pts"
mount -t tmpfs tmpfs "$ROOT/dev/shm"
mount -t proc proc "$ROOT/proc"
mount -t sysfs sysfs "$ROOT/sys"
cleanup() {
	for m in sys proc dev/shm dev/pts dev; do
		mountpoint -q "$ROOT/$m" && umount "$ROOT/$m" || true
	done
	rm -f "$ROOT/usr/bin/qemu-aarch64-static"
}
trap cleanup EXIT

in_root() {
	chroot "$ROOT" env -i \
		PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
		HOME=/root TERM=dumb LANG=C.UTF-8 \
		"$@"
}

# ------------------------------------------------------------------ packages
log "dnf install"
mapfile -t WANTED < <(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' -e 's/[[:space:]]//g' "$PKG_LIST")
INSTALL=()
for pkg in "${WANTED[@]}"; do
	in_root rpm -q "$pkg" >/dev/null 2>&1 && continue
	INSTALL+=("$pkg")
done
in_root dnf -y install --skip-unavailable --skip-broken "${INSTALL[@]}"
in_root dnf -y install sudo || true

# Fedora 43 does not build squeekboard any more; the Fedora 42 arm64 RPM
# still provides the OSK Phosh expects.  Best effort: if its sonames moved,
# maliit-keyboard remains installed as a fallback.
SQUEEKBORD_RPM=https://kojipkgs.fedoraproject.org/packages/squeekboard/1.43.1/2.fc42/aarch64/squeekboard-1.43.1-2.fc42.aarch64.rpm
in_root dnf -y install alternatives || true
in_root dnf -y install --skip-unavailable --skip-broken "$SQUEEKBORD_RPM" || true
# Phosh 0.54 requires the "sm.puri.OSK0" component, while squeekboard 1.43
# ships sm.puri.Squeekboard.  Its alternatives link fails in the WSL base;
# create the alias so gnome-session can find the OSK component.
if [ ! -e "$ROOT/usr/share/applications/sm.puri.OSK0.desktop" ] && \
   [ -e "$ROOT/usr/share/applications/sm.puri.Squeekboard.desktop" ]; then
	mkdir -p "$ROOT/etc/alternatives"
	ln -sf /usr/share/applications/sm.puri.Squeekboard.desktop \
		"$ROOT/etc/alternatives/sm.puri.OSK0.desktop"
	ln -sf /etc/alternatives/sm.puri.OSK0.desktop \
		"$ROOT/usr/share/applications/sm.puri.OSK0.desktop"
fi
# Phosh 0.54 lists sm.puri.OSK0 as a required gnome-session component, but
# Fedora 42's squeekboard only provides sm.puri.Squeekboard.  Drop the
# component so gnome-session can start; squeekboard's autostart still runs.
SESS="$ROOT/usr/share/gnome-session/sessions/phosh.session"
if [ -f "$SESS" ]; then
	sed -i 's/;sm\.puri\.OSK0//g; s/sm\.puri\.OSK0;//g' "$SESS"
fi
if ! in_root rpm -q phosh phoc gnome-session >/dev/null 2>&1 || ! in_root command -v phosh-session >/dev/null 2>&1; then
	echo "Phosh packages missing from the Fedora repos" >&2
	exit 1
fi

# --------------------------------------------------------------- users/ssh
log "user c7"
for g in wheel video render input network; do
	in_root getent group "$g" >/dev/null 2>&1 || in_root groupadd "$g" || true
done
in_root useradd -m -G wheel,video,render,input,network -s /bin/bash c7 || true
printf 'c7:147147\n' | in_root chpasswd
mkdir -p "$ROOT/etc/sudoers.d"
printf '%%wheel ALL=(ALL) ALL\n' > "$ROOT/etc/sudoers.d/wheel"
chmod 0440 "$ROOT/etc/sudoers.d/wheel"

log "ssh host keys"
in_root ssh-keygen -A || true

# --------------------------------------------------------------- overlay
log "overlay"
# Fedora ships /usr/local/sbin as a symlink; the overlay contains a real
# directory, and cp refuses to replace a symlink with a directory.
for d in usr/local/bin usr/local/sbin; do
	if [ -L "$ROOT/$d" ]; then
		rm -f "$ROOT/$d"
	fi
	mkdir -p "$ROOT/$d"
done
cp -a "$HERE/overlay/." "$ROOT/"
chmod 0755 "$ROOT/usr/local/bin/c7-start-phosh" \
	"$ROOT/usr/local/bin/c7-start-denial" \
	"$ROOT/usr/local/bin/c7-start-gnome" \
	"$ROOT/usr/local/sbin/c7-usb-gadget.sh" 2>/dev/null || true
chmod 0600 "$ROOT/etc/NetworkManager/system-connections/usb0.nmconnection" 2>/dev/null || true

# ------------------------------------------------------------- selinux/fw
log "selinux/firewall"
mkdir -p "$ROOT/etc/selinux"
printf 'SELINUX=disabled\nSELINUXTYPE=targeted\n' > "$ROOT/etc/selinux/config"
in_root systemctl disable firewalld >/dev/null 2>&1 || true
in_root systemctl mask firewalld >/dev/null 2>&1 || true

# ---------------------------------------------------- kernel modules/firmware
log "kernel modules/firmware"
KREL=$(cat "$BASE/kernel-release")
mkdir -p "$ROOT/lib/modules" "$ROOT/lib/firmware"
cp -a "$BASE/lib/modules/$KREL" "$ROOT/lib/modules/"
cp -a --remove-destination "$BASE/lib/firmware/." "$ROOT/lib/firmware/"
if [ -d "$BASE/usr/bin" ]; then
	mkdir -p "$ROOT/usr/bin"
	cp -a "$BASE/usr/bin/." "$ROOT/usr/bin/"
	chmod 0755 "$ROOT/usr/bin/msm-firmware-loader"*.sh 2>/dev/null || true
fi
in_root depmod -a "$KREL" || true

# ------------------------------------------------------------------ services
log "enabling services"
for unit in \
	sshd.service \
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

# ------------------------------------------------------------------ cleanup
log "cleaning up"
in_root dnf clean all >/dev/null 2>&1 || true
rm -f "$ROOT/var/lib/dnf/rpmdb_lock.sqlite" 2>/dev/null || true
