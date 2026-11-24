#!/bin/bash
# Fallback for .github/workflows/ubuntu.yml when the "Build Nura for SM-C7000"
# artifact is unavailable (expired, cancelled, ...): rebuild boot.img and the
# pmOS root.img here with pmbootstrap, exactly like .github/workflows/build.yml
# does, so the rest of the Ubuntu pipeline is unchanged.
#
# Mirrors build.yml step by step on purpose - if that workflow changes, this
# script has to follow.  Run as the normal runner user (pmbootstrap refuses
# root); it uses sudo only where build.yml does.
set -euo pipefail

OUT=${1:-base}
OUT=$(readlink -m "$OUT")
REPO=$(readlink -m "$(dirname "$0")/..")

PMB_WORK=$REPO/.pmb-work
PMAPORTS=$PMB_WORK/cache_git/pmaports
DEVICE=samsung-c7lte
KPKG=linux-postmarketos-qcom-msm8953-c7
PMAPORTS_REF=${PMAPORTS_REF:-main}
KERNEL_TAG=v7.1.3-r0

step() { printf '\n==> %s\n' "$*"; }

step "installing host tools"
sudo apt-get update
sudo apt-get install -y --no-install-recommends git python3 e2fsprogs xz-utils

step "cloning pmbootstrap and pmaports"
rm -rf "$REPO/pmbootstrap" "$PMAPORTS"
git clone --depth=1 https://gitlab.nura.eco/nura/pmbootstrap.git "$REPO/pmbootstrap"
sudo ln -sf "$REPO/pmbootstrap/pmbootstrap.py" /usr/local/bin/pmbootstrap
mkdir -p "$PMB_WORK/cache_git"
git clone --depth=1 https://gitlab.nura.eco/nura/pmaports.git "$PMAPORTS"

# The repository's pmaports/ directory is the source of truth for the device
# package; the clone above only provides everything else (channels.cfg, ...).
cp -a "$REPO/pmaports/." "$PMAPORTS/"
(
	cd "$PMAPORTS"
	git remote add postmarketos https://gitlab.postmarketos.org/postmarketOS/pmaports.git
	# channels.cfg is looked up through this remote; no network needed for it.
	git update-ref refs/remotes/postmarketos/main HEAD
)

step "configuring pmbootstrap"
mkdir -p "$HOME/.config"
cat > "$HOME/.config/pmbootstrap_v3.cfg" <<EOF
[pmbootstrap]
aports = $PMAPORTS
work = $PMB_WORK
device = $DEVICE
kernel = $KPKG
ui = console
user = user
hostname = c7lte
locale = en_US.UTF-8
timezone = Asia/Shanghai
jobs = $(nproc)
service_manager = openrc
EOF
mkdir -p "$PMB_WORK/cache_ccache_aarch64" "$PMB_WORK/cache_ccache_x86_64" \
	"$PMB_WORK/cache_sccache" "$PMB_WORK/cache_distfiles"
sudo chown -R 1000:1000 "$PMB_WORK"/cache_* 2>/dev/null || true
yes "" | pmbootstrap init >/dev/null

step "prefetching the kernel source tarball"
# GitHub tarball downloads are rate limited; a cached copy in cache_distfiles
# makes pmbootstrap skip the download and shortens every rebuild.
TARBALL=$PMB_WORK/cache_distfiles/$KPKG-$KERNEL_TAG.tar.gz
if [ ! -s "$TARBALL" ]; then
	for attempt in $(seq 1 12); do
		if curl -fL --retry 3 --connect-timeout 30 --max-time 1800 \
			-o "$TARBALL.part" \
			"https://codeload.github.com/msm8953-mainline/linux/tar.gz/refs/tags/$KERNEL_TAG"; then
			mv "$TARBALL.part" "$TARBALL"
			break
		fi
		echo "download attempt $attempt failed, sleeping"
		rm -f "$TARBALL.part"
		sleep $((attempt * 10))
	done
fi
[ -s "$TARBALL" ] || { echo "could not fetch $TARBALL" >&2; exit 1; }

step "recomputing package checksums"
# "pmbootstrap build" verifies sha512sums, and the local sources in this
# repository do not match the hashes recorded in the pmaports clone.
python3 - "$PMAPORTS" "$DEVICE" "$KPKG" "$KERNEL_TAG" <<'PY'
import hashlib, os, re, sys

pmaports, device, kpkg, tag = sys.argv[1:5]


def sum512(path):
    h = hashlib.sha512()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def refresh(apkbuild, local_map):
    text = open(apkbuild).read()
    m = re.search(r"^sha512sums=\"(.*?)\"\n", text, re.S | re.M)
    if not m:
        print("  %s: no sha512sums" % apkbuild)
        return
    block = m.group(1)
    sums = [s for s in re.split(r"\s+", block.strip()) if s]
    out = []
    for entry in sums:
        name = entry.split("::")[-1]
        if name in local_map:
            out.append(sum512(local_map[name]))
        else:
            out.append(entry)
    new = re.sub(r"^sha512sums=\"(.*?)\"\n",
                 'sha512sums="%s"\n' % "\n".join(out), text, count=1,
                 flags=re.S | re.M)
    open(apkbuild, "w").write(new)
    print("  %s: %d hashes" % (apkbuild, len(out)))


dev = os.path.join(pmaports, "device/testing", "device-" + device)
refresh(os.path.join(dev, "APKBUILD"),
        {n: os.path.join(dev, n) for n in os.listdir(dev)})

kp = os.path.join(pmaports, "device/testing", kpkg)
locals_ = {n: os.path.join(kp, n) for n in os.listdir(kp)}
tarball = "%s-%s.tar.gz" % (kpkg, tag)
candidate = os.path.join(sys.argv[5], tarball) if len(sys.argv) > 5 else ""
if candidate and os.path.exists(candidate):
    locals_[tarball] = candidate
refresh(os.path.join(kp, "APKBUILD"), locals_)
PY

step "building the kernel and device packages"
# ~60-70 minutes cold; the kernel tarball is already local.
pmbootstrap build "$KPKG" "device-$DEVICE"

step "pmbootstrap install --split"
images=$PMB_WORK/chroot_native/home/pmos/rootfs
for attempt in 1 2; do
	if pmbootstrap -y --details-to-stdout install --split --password 147147; then
		break
	fi
	echo "install attempt $attempt failed"
	[ "$attempt" = 2 ] && exit 1
done
[ -s "$images/$DEVICE-root.img" ] && [ -s "$images/$DEVICE-boot.img" ] || {
	echo "install did not produce images in $images" >&2
	exit 1
}

step "exporting"
pmbootstrap export >/dev/null 2>&1 || true
mkdir -p "$OUT"
cp /tmp/postmarketOS-export/boot.img "$OUT/boot.img"
cp "/tmp/postmarketOS-export/$DEVICE-root.img" "$OUT/$DEVICE-root.img"

# Pin the root filesystem UUID, like build.yml does, so that the boot image we
# patch and the rootfs we ship always agree.
sudo tune2fs -U 6fb8f25b-07b4-4934-ab41-0921b7bc6a6c "$OUT/$DEVICE-root.img"
ls -l "$OUT"
echo "==> fallback base images in $OUT"
