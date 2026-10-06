#!/usr/bin/env python3

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def fail(message: str) -> int:
    print(f"pcps4: error: {message}", file=sys.stderr)
    return 1

def main() -> int:
    toolchain = os.environ.get("OO_PS4_TOOLCHAIN")
    if not toolchain:
        return fail("OO_PS4_TOOLCHAIN is not set.")

    root = Path(toolchain)
    if not root.is_dir():
        return fail(f"Toolchain directory does not exist: {toolchain}")

    for program in ("clang", "ld.lld"):
        if shutil.which(program) is None:
            return fail(f"{program} was not found in PATH.")

    create_fself = root / "bin" / "linux" / "create-fself"
    if not create_fself.is_file():
        return fail(f"create-fself was not found: {create_fself}")

    print(f"OpenOrbis: {root}")
    print("Target: x86_64-pc-freebsd12-elf")
    print(f"create-fself: {create_fself}")

    return subprocess.run(["make", "-C", str(ROOT)]).returncode

if __name__ == "__main__":
    raise SystemExit(main())
