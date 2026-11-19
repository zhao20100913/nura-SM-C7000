#!/usr/bin/env python3
"""Fetch the kernel + initramfs (boot.img) and the pmOS rootfs (root.img).

We deliberately do *not* rebuild the kernel here.  The repository's own
``Build Nura for SM-C7000`` workflow already produces a boot image that this
device's aboot accepts (Android boot header v0, 2048-byte pages, QCDT in the
``dt`` area, ``SEANDROIDENFORCE`` trailer, see docs/03-flashing.md) and a root
filesystem that carries the matching ``/lib/modules`` tree and firmware.

This script downloads the newest usable ``nura-c7-*`` artifact of that workflow
and unpacks ``boot.img`` plus ``<device>-root.img`` into the output directory.
"""

import io
import json
import os
import shutil
import sys
import urllib.error
import urllib.request
import zipfile

API = "https://api.github.com"
MIN_SIZE = 100 * 1024 * 1024  # smaller artifacts are failed/partial builds


def api_get(url: str, token: str) -> bytes:
    req = urllib.request.Request(
        url,
        headers={
            "Authorization": "token %s" % token,
            "Accept": "application/vnd.github+json",
            "User-Agent": "c7-ubuntu-builder",
        },
    )
    with urllib.request.urlopen(req, timeout=300) as resp:
        return resp.read()


def pick_artifact(repo: str, token: str, wanted_id: str) -> dict:
    if wanted_id:
        art = json.loads(api_get("%s/repos/%s/actions/artifacts/%s" % (API, repo, wanted_id), token))
        if art.get("expired"):
            sys.exit("artifact %s has expired" % wanted_id)
        return art

    listing = json.loads(
        api_get("%s/repos/%s/actions/artifacts?per_page=100" % (API, repo), token)
    )["artifacts"]
    candidates = [
        a
        for a in listing
        if a["name"].startswith("nura-c7-")
        and not a["expired"]
        and a["size_in_bytes"] > MIN_SIZE
    ]
    candidates.sort(key=lambda a: a["created_at"], reverse=True)
    if not candidates:
        sys.exit(
            "no usable nura-c7-* artifact found; run the 'Build Nura for "
            "SM-C7000' workflow first (or pass base_artifact_id)"
        )
    return candidates[0]


def main() -> None:
    out = sys.argv[1] if len(sys.argv) > 1 else "base"
    repo = os.environ.get("GITHUB_REPOSITORY") or sys.exit("GITHUB_REPOSITORY is not set")
    token = os.environ.get("GITHUB_TOKEN") or sys.exit("GITHUB_TOKEN is not set")
    wanted = os.environ.get("BASE_ARTIFACT_ID", "").strip()

    art = pick_artifact(repo, token, wanted)
    print(
        "using artifact %s (id=%s, %.1f MiB, created %s)"
        % (art["name"], art["id"], art["size_in_bytes"] / 1048576, art["created_at"])
    )

    blob = api_get(
        "%s/repos/%s/actions/artifacts/%s/zip" % (API, repo, art["id"]), token
    )
    print("downloaded %.1f MiB" % (len(blob) / 1048576))

    os.makedirs(out, exist_ok=True)
    with zipfile.ZipFile(io.BytesIO(blob)) as zf:
        names = zf.namelist()
        print("artifact entries: %s" % ", ".join(names))
        boot = [n for n in names if os.path.basename(n) == "boot.img"]
        root = [n for n in names if n.endswith("-root.img")]
        if not boot:
            sys.exit("artifact has no boot.img")
        if not root:
            sys.exit("artifact has no *-root.img")
        root.sort(key=len)  # prefer ``samsung-c7lte-root.img`` over ``.../root.img``

        def unpack(name: str, dest: str) -> None:
            with zf.open(name) as src, open(dest, "wb") as dst:
                shutil.copyfileobj(src, dst, 1024 * 1024)
            print("%-40s -> %s (%.1f MiB)" % (name, dest, os.path.getsize(dest) / 1048576))

        unpack(boot[0], os.path.join(out, "boot.img"))
        unpack(root[0], os.path.join(out, "samsung-c7lte-root.img"))
        for extra in names:
            base = os.path.basename(extra)
            if base.endswith(".dtb") or base == "build.log":
                unpack(extra, os.path.join(out, base))

    with open(os.path.join(out, "SOURCE_ARTIFACT.json"), "w") as fh:
        json.dump(art, fh, indent=2)


if __name__ == "__main__":
    main()
