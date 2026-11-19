#!/usr/bin/env python3
"""Patch the kernel command line of an Android boot v0 image.

The Galaxy C7 aboot (Samsung's bootloader) wants a *stock style* Android boot
image: header v0, 2048-byte pages, a QCDT table in the ``dt`` area and the
``SEANDROIDENFORCE`` marker at the very end (see docs/03-flashing.md).

The postmarketOS initramfs that the Nura CI produces finds its root filesystem
by ``pmos_root_uuid=<uuid>``, so when the root filesystem is replaced by an
Ubuntu one the command line has to be rewritten.  Everything else in the image
(kernel, initramfs, QCDT) is left untouched - this tool only flips bytes inside
the 512-byte ``cmdline`` field of the header.

Usage::

    python3 patch-bootimg-cmdline.py --input boot.img --output out.img \
        --root-uuid 1234... --drop lk2nd.pass-ramoops

Exit code is non-zero when the input is not a boot v0 image with the layout the
C7 expects.
"""

import argparse
import re
import struct
import sys

ANDROID_MAGIC = b"ANDROID!"
PAGE_SIZES = (2048, 4096)
CMDLINE_OFFSET = 64      # boot_img_hdr_v0: after magic + 8 u32 + name[16]
CMDLINE_LEN = 512        # BOOT_ARGS_SIZE
SEANDROIDENFORCE = b"SEANDROIDENFORCE"

# Offsets inside boot_img_hdr_v0 (little endian, all u32 except the magic).
OFF = {
    "kernel_size": 8,
    "kernel_addr": 12,
    "ramdisk_size": 16,
    "ramdisk_addr": 20,
    "second_size": 24,
    "second_addr": 28,
    "tags_addr": 32,
    "page_size": 36,
    "dt_size": 40,
    "name": 48,
}


def parse_header(data: bytes) -> dict:
    if data[:8] != ANDROID_MAGIC:
        sys.exit("error: not an Android boot image (magic %r)" % data[:8])
    page = struct.unpack_from("<I", data, OFF["page_size"])[0]
    if page not in PAGE_SIZES:
        sys.exit("error: unsupported page size %d (expected 2048 or 4096)" % page)
    hdr = {k: struct.unpack_from("<I", data, v)[0] for k, v in OFF.items() if k not in ("name",)}
    hdr["page_size"] = page
    raw = data[CMDLINE_OFFSET:CMDLINE_OFFSET + CMDLINE_LEN]
    hdr["cmdline"] = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
    return hdr


def set_cmdline(data: bytearray, cmdline: str) -> None:
    raw = cmdline.encode()
    if len(raw) > CMDLINE_LEN:
        sys.exit("error: command line is %d bytes, limit is %d" % (len(raw), CMDLINE_LEN))
    data[CMDLINE_OFFSET:CMDLINE_OFFSET + CMDLINE_LEN] = raw.ljust(CMDLINE_LEN, b"\0")


def rewrite(cmdline: str, root_uuid: str, drop: list) -> str:
    parts = cmdline.split()
    out = []
    seen_root = False
    for token in parts:
        if token in drop:
            continue
        if token.startswith("pmos_root_uuid="):
            out.append("pmos_root_uuid=%s" % root_uuid)
            seen_root = True
            continue
        # A stale pmos_root= wins over the UUID in some initramfs helpers.
        if token.startswith("pmos_root=") and not token.startswith("pmos_root_uuid="):
            continue
        out.append(token)
    if not seen_root:
        out.append("pmos_root_uuid=%s" % root_uuid)
    if not any(t.startswith("console=") for t in out):
        out.insert(0, "console=ttyMSM0,115200n8")
    return " ".join(out)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", required=True)
    ap.add_argument("--output", required=True)
    ap.add_argument("--root-uuid", required=True,
                    help="UUID of the new root filesystem (pmos_root_uuid)")
    ap.add_argument("--drop", action="append", default=[],
                    help="token to remove from the command line (repeatable)")
    args = ap.parse_args()

    data = bytearray(open(args.input, "rb").read())
    hdr = parse_header(data)

    if not re.fullmatch(r"[0-9a-fA-F-]{36}", args.root_uuid):
        sys.exit("error: --root-uuid does not look like a UUID: %r" % args.root_uuid)

    old = hdr["cmdline"]
    new = rewrite(old, args.root_uuid, [d for d in args.drop if d])

    page = hdr["page_size"]
    kernel_end = page + hdr["kernel_size"]
    ramdisk_end = kernel_end + ((hdr["ramdisk_size"] + page - 1) // page) * page
    dt_end = ramdisk_end + ((hdr["dt_size"] + page - 1) // page) * page if hdr["dt_size"] else ramdisk_end
    tail = bytes(data[-len(SEANDROIDENFORCE):])

    print("input:            %s" % args.input)
    print("page size:        %d" % page)
    print("kernel:           %d bytes" % hdr["kernel_size"])
    print("ramdisk:          %d bytes" % hdr["ramdisk_size"])
    print("dt (QCDT):        %d bytes" % hdr["dt_size"])
    print("payload end:      %d bytes" % dt_end)
    print("file size:        %d bytes" % len(data))
    print("SEANDROIDENFORCE: %s" % ("yes" if tail == SEANDROIDENFORCE else "MISSING"))
    print("old cmdline:      %s" % old)
    print("new cmdline:      %s" % new)
    if hdr["dt_size"] == 0:
        print("warning: dt area is empty; the C7 bootloader expects a QCDT table", file=sys.stderr)
    if tail != SEANDROIDENFORCE:
        print("warning: image does not end with SEANDROIDENFORCE", file=sys.stderr)

    set_cmdline(data, new)
    with open(args.output, "wb") as fh:
        fh.write(data)
    print("wrote %s" % args.output)


if __name__ == "__main__":
    main()
