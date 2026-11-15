#!/usr/bin/env python3
"""List or extract firmware blobs from a Samsung apnhlos/modem FAT16 image.

Samsung ships the PIL blobs as a FAT16 filesystem inside the
``NON-HLOS.bin`` (flashed to ``apnhlos``) and ``modem.bin`` (flashed to
``modem``) files of the Odin firmware package.  The phone mounts those
partitions directly, so nothing has to be extracted for normal operation;
this helper exists to inspect them (and to recover the WLAN NV blob from
``system.img`` when the stock image is all you have).

Examples::

    python3 tools/dump-fw.py NON-HLOS.bin                  # list blobs
    python3 tools/dump-fw.py NON-HLOS.bin -o out/          # extract all
    python3 tools/dump-fw.py NON-HLOS.bin --grep WCNSS     # only WCNSS.*
"""

import argparse
import os
import struct
import sys


class Fat16:
    def __init__(self, data: bytes):
        self.data = data
        bs = data[:512]
        self.bps = struct.unpack("<H", bs[11:13])[0]
        self.spc = bs[13]
        self.reserved = struct.unpack("<H", bs[14:16])[0]
        self.num_fats = bs[16]
        self.root_entries = struct.unpack("<H", bs[17:19])[0]
        self.fat_size = struct.unpack("<H", bs[22:24])[0]
        self.fat_off = self.reserved * self.bps
        self.root_off = (self.reserved + self.num_fats * self.fat_size) * self.bps
        self.data_off = self.root_off + self.root_entries * 32
        self.cluster_bytes = self.spc * self.bps

    def _fat(self, cluster: int) -> int:
        off = self.fat_off + cluster * 2
        return struct.unpack("<H", self.data[off:off + 2])[0]

    def _cluster_data(self, cluster: int) -> bytes:
        out = bytearray()
        while 2 <= cluster < 0xFFF8:
            start = self.data_off + (cluster - 2) * self.cluster_bytes
            out += self.data[start:start + self.cluster_bytes]
            cluster = self._fat(cluster)
        return bytes(out)

    def walk(self, buf: bytes, prefix: str = ""):
        for i in range(len(buf) // 32):
            ent = buf[i * 32:(i + 1) * 32]
            if ent[0] == 0:
                return
            if ent[0] == 0xE5:
                continue
            attr = ent[11]
            name = ent[0:8].decode("latin1").rstrip()
            ext = ent[8:11].decode("latin1").rstrip()
            full = name + ("." + ext if ext else "")
            first = struct.unpack("<H", ent[26:28])[0]
            size = struct.unpack("<I", ent[28:32])[0]
            if attr & 0x10:
                if full not in (".", ".."):
                    yield from self.walk(self._cluster_data(first), prefix + full + "/")
                continue
            yield prefix + full, size, self._cluster_data(first)[:size]

    def entries(self):
        root = self.data[self.root_off:self.root_off + self.root_entries * 32]
        yield from self.walk(root)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="NON-HLOS.bin / modem.bin / raw partition dump")
    ap.add_argument("-o", "--out", help="extract into this directory")
    ap.add_argument("--grep", help="only entries whose name contains this substring")
    args = ap.parse_args()

    data = open(args.image, "rb").read()
    if data[0:3] != b"\xeb\x3c\x90":
        print(f"{args.image}: not a FAT16 boot sector", file=sys.stderr)
        return 1

    fat = Fat16(data)
    count = 0
    for name, size, payload in fat.entries():
        if args.grep and args.grep.lower() not in name.lower():
            continue
        count += 1
        if args.out:
            dest = os.path.join(args.out, name)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            with open(dest, "wb") as fh:
                fh.write(payload)
            print(f"{size:>10}  {name}")
        else:
            print(f"{size:>10}  {name}")
    print(f"{count} entr{'y' if count == 1 else 'ies'}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
