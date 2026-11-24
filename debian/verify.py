#!/usr/bin/env python3
"""Sanity check the Ubuntu flash package before it is uploaded as an artifact.

Everything here mirrors what the C7 bootloader and the postmarketOS initramfs
in boot.img actually require (docs/03-flashing.md), plus the ext4 pieces the
initramfs uses to find and grow the root filesystem.
"""

import os
import re
import struct
import sys

SEANDROIDENFORCE = b"SEANDROIDENFORCE"
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
}

failures = []


def check(cond, msg):
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond:
        failures.append(msg)


def check_bootimg(path, root_uuid):
    print("boot image: %s" % path)
    data = open(path, "rb").read()
    check(data[:8] == b"ANDROID!", "Android boot magic")
    page = struct.unpack_from("<I", data, OFF["page_size"])[0]
    check(page == 2048, "page size is 2048 (got %d)" % page)
    hdr = {k: struct.unpack_from("<I", data, v)[0] for k, v in OFF.items()}
    # mkbootimg -base 0x80000000 writes absolute addresses; docs/03-flashing.md
    # talks about the 0x8000 / 0x01000000 / 0x0100 offsets, so compare both.
    base = hdr["kernel_addr"] - 0x8000
    check(base == 0x80000000, "kernel load address is base+0x8000 (got 0x%08x)" % hdr["kernel_addr"])
    check(hdr["ramdisk_addr"] - base == 0x01000000,
          "ramdisk load address is base+0x01000000 (got 0x%08x)" % hdr["ramdisk_addr"])
    check(hdr["tags_addr"] - base == 0x0100,
          "tags address is base+0x0100 (got 0x%08x)" % hdr["tags_addr"])
    check(hdr["second_size"] == 0, "no second stage")

    kstart = page
    kend = kstart + hdr["kernel_size"]
    rstart = kend + (-kend % page)
    rend = rstart + hdr["ramdisk_size"]
    dstart = rend + (-rend % page)
    check(hdr["kernel_size"] > 0, "kernel present (%d bytes)" % hdr["kernel_size"])
    check(data[kstart:kstart + 2] == b"\x1f\x8b", "kernel is gzip compressed")
    check(hdr["ramdisk_size"] > 0, "ramdisk present (%d bytes)" % hdr["ramdisk_size"])
    check(
        data[rstart:rstart + 4] in (b"\x28\xb5\x2f\xfd", b"\x1f\x8b", b"070701"),
        "ramdisk is zstd/gzip/cpio",
    )
    check(hdr["dt_size"] > 0, "dt area present (%d bytes)" % hdr["dt_size"])
    if hdr["dt_size"] > 0:
        check(data[dstart:dstart + 4] == b"QCDT", "dt area is a QCDT table (not an appended dtb)")
        entries = struct.unpack_from("<I", data, dstart + 8)[0]
        print("       QCDT entries: %d" % entries)
        # QCDT v2 entry: soc_id, variant_id, subtype, pad, offset, size (24 bytes)
        ok = entries > 0
        subtypes = []
        for i in range(entries):
            entry_at = dstart + 12 + i * 24
            if entry_at + 24 > len(data):
                ok = False
                break
            soc, variant, subtype, _pad, off, size = struct.unpack_from("<6I", data, entry_at)
            subtypes.append(subtype)
            print("       entry %d: soc 0x%x variant 0x%x subtype %d offset 0x%x size 0x%x"
                  % (i, soc, variant, subtype, off, size))
            if soc != 0x125 or variant != 0x8:
                ok = False
            if off < 12 + 24 * entries or off + size > hdr["dt_size"]:
                ok = False
        check(ok, "QCDT entries are msm8953/board 8 and point inside the dt area")
        check(sorted(subtypes) == [0, 3, 4], "QCDT subtypes are 0, 3 and 4 (got %s)" % subtypes)
    check(data[-len(SEANDROIDENFORCE):] == SEANDROIDENFORCE, "SEANDROIDENFORCE trailer")

    cmdline = data[64:64 + 512].split(b"\0", 1)[0].decode()
    print("       cmdline: %s" % cmdline)
    check("console=ttyMSM0,115200n8" in cmdline, "console=ttyMSM0,115200n8 on the command line")
    check("pmos_root_uuid=%s" % root_uuid in cmdline, "pmos_root_uuid matches the root image")
    check("lk2nd" not in cmdline, "no lk2nd dependency on the command line")


def ext4_info(path):
    with open(path, "rb") as fh:
        fh.seek(1024 + 0x38)
        magic = struct.unpack("<H", fh.read(2))[0]
        fh.seek(1024 + 0x68)
        uuid_raw = fh.read(16)
        fh.seek(1024 + 0x78)
        label = fh.read(16).split(b"\0", 1)[0].decode()
    uuid = "-".join(
        [
            uuid_raw[0:4].hex(),
            uuid_raw[4:6].hex(),
            uuid_raw[6:8].hex(),
            uuid_raw[8:10].hex(),
            uuid_raw[10:16].hex(),
        ]
    )
    return magic, uuid, label


def check_rootimg(path, root_uuid):
    print("root image: %s" % path)
    magic, uuid, label = ext4_info(path)
    check(magic == 0xEF53, "ext4 superblock magic")
    check(uuid == root_uuid, "ext4 UUID is %s (expected %s)" % (uuid, root_uuid))
    check(label == "debian-root", "ext4 label is %r" % label)
    size = os.path.getsize(path)
    check(size > 512 * 1024 * 1024, "image is %d bytes" % size)
    check(size % 512 == 0, "image size is a multiple of 512")


def main():
    if len(sys.argv) < 4:
        sys.exit("usage: verify.py <boot.img> <root.img> <uuid-file>")
    boot, root, uuid_file = sys.argv[1:4]
    root_uuid = open(uuid_file).read().strip()
    check(re.fullmatch(r"[0-9a-f-]{36}", root_uuid) is not None, "root UUID looks sane")
    check_bootimg(boot, root_uuid)
    check_rootimg(root, root_uuid)
    print()
    if failures:
        print("FAILED: %d check(s)" % len(failures))
        sys.exit(1)
    print("all checks passed")


if __name__ == "__main__":
    main()
