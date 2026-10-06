#!/usr/bin/env python3
"""Patch a GP4 project with an arbitrary recursively staged directory.

The OpenOrbis create-gp4 binary shipped by some toolchain releases only
supports the -files option.  This helper adds a recursive directory tree to
an already valid GP4 skeleton while preserving the existing package entries.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path, PurePosixPath
import xml.etree.ElementTree as ET


def ensure_dir(parent: ET.Element, name: str) -> ET.Element:
    for child in parent.findall("dir"):
        if child.get("targ_name") == name:
            return child
    return ET.SubElement(parent, "dir", {"targ_name": name})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("gp4", type=Path)
    parser.add_argument("source_dir", type=Path)
    parser.add_argument("--target-root", default=None)
    args = parser.parse_args()

    gp4 = args.gp4
    source_dir = args.source_dir

    if not gp4.is_file():
        raise SystemExit(f"GP4 not found: {gp4}")
    if not source_dir.is_dir():
        raise SystemExit(f"source directory not found: {source_dir}")

    tree = ET.parse(gp4)
    root = tree.getroot()

    rootdir = root.find("rootdir")
    files = root.find("files")
    if rootdir is None or files is None:
        raise SystemExit("GP4 is missing rootdir/files sections")

    target_root = args.target_root or source_dir.name
    target_root = target_root.replace("\\", "/").strip("/")
    if not target_root:
        raise SystemExit("target root must not be empty")

    root_map = ensure_dir(rootdir, target_root)

    existing_files = {
        item.get("targ_path")
        for item in files.findall("file")
        if item.get("targ_path")
    }

    added = 0
    root_resolved = source_dir.resolve()

    for path in sorted(source_dir.rglob("*")):
        if not path.is_file():
            continue

        rel = path.resolve().relative_to(root_resolved)
        rel_posix = PurePosixPath(rel.as_posix())
        parts = rel_posix.parts
        if not parts:
            continue

        parent = root_map
        for dirname in parts[:-1]:
            parent = ensure_dir(parent, dirname)

        target = "/".join((target_root, *parts))
        if target in existing_files:
            continue

        ET.SubElement(
            files,
            "file",
            {"targ_path": target, "orig_path": target},
        )
        existing_files.add(target)
        added += 1

    try:
        ET.indent(tree, space="\t")
    except AttributeError:
        pass
    tree.write(gp4, encoding="utf-8", xml_declaration=True)
    print(f"GP4 patched: added {added} file(s) under {target_root}/")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
