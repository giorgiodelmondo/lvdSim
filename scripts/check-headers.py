#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check (and optionally add) the SPDX licence line of lvdsim's own source files.

Usage:
    scripts/check-headers.py [--fix] [--root DIR]

Exit status 0 when every checked file has the line, 1 otherwise (so it can run in ctest or CI).
With --fix the line is inserted at the top (after a #! line, if any), keeping the file's line endings.

Checked: C/C++ sources and headers, CMake files, shell, Tcl, Python, PowerShell, batch files, the installer script
and the GLSL shaders. Not checked: third_party/ (own licences), kernel/ (NVC patches and notes), docs/, examples/
(demonstration designs), .claude/, generated and build directories.
"""
import argparse
import os
import sys

TAG = "SPDX-License-Identifier: GPL-3.0-or-later"

# extension (or exact file name) -> comment prefix
PREFIX = {
    ".c": "//", ".cpp": "//", ".h": "//", ".hpp": "//", ".vert": "//", ".frag": "//",
    ".cmake": "#", "CMakeLists.txt": "#", ".sh": "#", ".tcl": "#", ".py": "#", ".ps1": "#",
    ".bat": "rem", ".iss": ";",
}
SKIP_DIRS = {".git", "build", "third_party", "kernel", "docs", "examples", ".claude", "__pycache__"}


def prefix_for(name):
    if name in PREFIX:
        return PREFIX[name]
    return PREFIX.get(os.path.splitext(name)[1].lower())


def skipped(rel):
    parts = rel.replace("\\", "/").split("/")
    if parts[0] == "kernel":
        return not (len(parts) >= 2 and parts[1] == "build")
    return any(p in SKIP_DIRS or p.startswith("build") for p in parts[:-1]) or parts[0] in SKIP_DIRS


def files(root):
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        dirnames[:] = [d for d in dirnames if not skipped(os.path.join(rel_dir, d, "x"))]
        for f in filenames:
            full = os.path.join(dirpath, f)
            p = prefix_for(f)
            if p and not skipped(os.path.relpath(full, root)):
                yield full, p


def has_tag(data):
    head = data[:600].decode("utf-8", "replace")
    return TAG in head


def fix(path, prefix, data):
    nl = b"\r\n" if b"\r\n" in data else b"\n"
    line = (prefix + " " + TAG).encode() + nl
    if data.startswith(b"\xef\xbb\xbf"):
        bom, data = data[:3], data[3:]
    else:
        bom = b""
    if data.startswith(b"#!"):
        idx = data.find(b"\n") + 1
        data = data[:idx] + line + data[idx:]
    else:
        data = line + data
    return bom + data


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--fix", action="store_true", help="insert the missing lines")
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    missing = []
    for path, prefix in sorted(files(root)):
        with open(path, "rb") as fh:
            data = fh.read()
        if has_tag(data):
            continue
        missing.append(os.path.relpath(path, root))
        if args.fix:
            new = fix(path, prefix, data)
            with open(path, "wb") as fh:
                fh.write(new)
    for m in missing:
        print(("fixed: " if args.fix else "missing: ") + m)
    if missing and not args.fix:
        print(f"{len(missing)} file(s) without '{TAG}'; run scripts/check-headers.py --fix")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
