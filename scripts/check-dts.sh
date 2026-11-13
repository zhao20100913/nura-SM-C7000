#!/bin/sh
# Compile the C7 device tree locally, without a full kernel build.
#
# Usage:   ./scripts/check-dts.sh [kernel-tag]      (default: v7.1.3-r0)
# Needs:   git, curl, cpp or clang, dtc, and network access to
#          raw.githubusercontent.com (one-shot; the tree is cached in .dts-cache/).
#
# Why: the CI kernel build fails on DTS syntax after ~2 minutes, but a full
# round trip is still slow when you are editing the tree.  This script
# reconstructs just the pieces dtc needs: the two SoC includes, the dt-bindings
# headers they pull in, and the board dts itself.
set -eu

TAG=${1:-v7.1.3-r0}
SRC=https://raw.githubusercontent.com/msm8953-mainline/linux/$TAG
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CACHE="$ROOT/.dts-cache/$TAG"
QDIR="$CACHE/arch/arm64/boot/dts/qcom"

fetch() { # $1 = repo path relative to the kernel root
	mkdir -p "$CACHE/$(dirname "$1")"
	[ -s "$CACHE/$1" ] && return 0
	printf '  fetch %s\n' "$1"
	curl -fsSL "$SRC/$1" -o "$CACHE/$1"
}

fetch arch/arm64/boot/dts/qcom/msm8953.dtsi
fetch arch/arm64/boot/dts/qcom/pm8953.dtsi
fetch include/uapi/linux/input-event-codes.h
mkdir -p "$CACHE/include/dt-bindings/input"
cp "$CACHE/include/uapi/linux/input-event-codes.h" \
   "$CACHE/include/dt-bindings/input/linux-event-codes.h"

# Pull in whatever dt-bindings headers the includes ask for, one pass at a time.
i=0
while :; do
	i=$((i + 1))
	[ "$i" -lt 40 ] || { echo 'giving up: too many missing headers' >&2; exit 1; }
	if CPP_OUT=$(cpp -nostdinc -undef -D__DTS__ -P -x assembler-with-cpp \
		-I "$CACHE/include" -I "$QDIR" \
		"$ROOT/dts/msm8953-samsung-c7lte.dts" -o "$CACHE/board.dts" 2>&1); then
		break
	fi
	miss=$(printf '%s\n' "$CPP_OUT" | sed -n "s/.*'\([^']*\)' file not found.*/\1/p" | head -1)
	[ -n "$miss" ] || { printf '%s\n' "$CPP_OUT" >&2; exit 1; }
	fetch "include/$miss"
done

cp "$ROOT/dts/msm8953-samsung-c7lte.dts" "$QDIR/"
dtc -I dts -O dtb -o "$CACHE/board.dtb" "$CACHE/board.dts"
echo "ok: $CACHE/board.dtb"
