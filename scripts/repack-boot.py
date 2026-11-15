#!/usr/bin/env python3
"""Re-splice a new QCDT device-tree area into an existing Android boot.img.

Used by .github/workflows/dtb.yml: when only the board DTS changed, the
kernel and the initramfs are still valid, so the 70 minute kernel build is
wasted work.  This script keeps the original kernel/ramdisk/header and
replaces the dt area (QCDT table + dtbs), which is all that a DTS edit
changes.

Android boot header v0 layout (all little endian, page aligned blocks):
  0 magic "ANDROID!"   8 kernel_size   12 kernel_addr   16 ramdisk_size
  20 ramdisk_addr      24 second_size  28 second_addr   32 tags_addr
  36 page_size         40 dt_size      44 unused
  48 name[16]          64 cmdline[512] 576 id[8]         608 extra_cmdline[1024]
"""
import struct
import sys
import os

MAGIC = b"ANDROID!"


def qcdt(dtbs, pad_to=0x800):
    """Build a QCDT v2 blob for a list of (dtb_bytes, [(msm_id, board_id, subtype)])."""
    entries = []
    for blob, keys in dtbs:
        for (msm_id, board_id, subtype) in keys:
            entries.append((msm_id, board_id, subtype, blob))
    total = 12 + 24 * len(entries)
    off = max(total, pad_to)
    table = bytearray(b"QCDT")
    table += struct.pack("<II", 2, len(entries))
    body = bytearray()
    for (msm_id, board_id, subtype, blob) in entries:
        table += struct.pack("<IIIIII", msm_id, board_id, subtype, 0,
                             off + len(body), len(blob))
        body += blob
        if len(body) % 4:
            body += b"\0" * (4 - len(body) % 4)
    pad = off - len(table)
    table += b"\0" * pad
    return bytes(table) + bytes(body)


def main():
    src, dtb, out = sys.argv[1], sys.argv[2], sys.argv[3]
    boards = [(0x125, 8, 0), (0x125, 8, 3), (0x125, 8, 4)]
    data = open(src, "rb").read()
    assert data[:8] == MAGIC, "not an Android boot image"
    page = struct.unpack_from("<I", data, 36)[0]
    assert page in (2048, 4096, 8192, 16384), page
    kernel_size, ramdisk_size, second_size = struct.unpack_from("<III", data, 8)[0], \
        struct.unpack_from("<I", data, 16)[0], struct.unpack_from("<I", data, 24)[0]
    dt_size = struct.unpack_from("<I", data, 40)[0]

    def blocks(n):
        return (n + page - 1) // page

    base = page * (1 + blocks(kernel_size) + blocks(ramdisk_size) + blocks(second_size))
    tail = data[base + dt_size:]
    blob = qcdt([(open(dtb, "rb").read(), boards)])
    blob += b"\0" * ((-len(blob)) % page)
    head = bytearray(data[:base])
    struct.pack_into("<I", head, 40, len(blob))
    out_data = bytes(head) + blob + tail
    open(out, "wb").write(out_data)
    print("page=%d kernel=%d ramdisk=%d dt=%d tail=%d -> %s (%d B)"
          % (page, kernel_size, ramdisk_size, len(blob), len(tail), out, len(out_data)))
    # self-check: reparse
    chk = open(out, "rb").read()
    assert chk[:8] == MAGIC
    assert struct.unpack_from("<I", chk, 40)[0] == len(blob)
    assert chk[base:base + 4] == b"QCDT"


if __name__ == "__main__":
    main()
