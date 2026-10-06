#!/usr/bin/env python3
"""
PcPs4 shader conversion helper.

DXBC path:
  DXBC (D3D SM5.x)
      -> dxbc-spirv
      -> SPIR-V
      -> opengnm-psbc -4
      -> PS4 GNM shader binary

GLSL path is used by CI to generate a known-good hardware smoke shader.
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path


def run(command: list[str]) -> None:
    print("$", " ".join(command))
    subprocess.run(command, check=True)


def require(tool: str) -> str:
    path = shutil.which(tool)
    if not path:
        raise SystemExit(f"Required tool not found: {tool}")
    return path


def compile_glsl(src: Path, stage: str, output: Path) -> None:
    glslang = require("glslangValidator")
    run([glslang, "-V", "-S", stage, "-o", str(output), str(src)])


def compile_dxbc(src: Path, output: Path, compiler: Path) -> None:
    run([str(compiler), str(src), "--spv", str(output)])


def compile_ps4(spv: Path, stage: str, output: Path, psbc: Path) -> None:
    stage_name = {
        "vertex": "vertex",
        "fragment": "fragment",
        "pixel": "fragment",
    }[stage]
    run([
        str(psbc),
        "-s", stage_name,
        "-f", str(spv),
        "-o", str(output),
        "-4",
    ])


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--stage", choices=["vertex", "fragment", "pixel"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--kind", choices=["glsl", "dxbc"], required=True)
    parser.add_argument("--dxbc-compiler", type=Path)
    parser.add_argument("--psbc", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, default=Path("build/shader-work"))
    args = parser.parse_args()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.work_dir.mkdir(parents=True, exist_ok=True)

    spv = args.work_dir / (args.input.stem + ".spv")

    if args.kind == "glsl":
        compile_glsl(args.input, "vert" if args.stage == "vertex" else "frag", spv)
    else:
        if args.dxbc_compiler is None:
            raise SystemExit("--dxbc-compiler is required for DXBC input")
        compile_dxbc(args.input, spv, args.dxbc_compiler)

    compile_ps4(spv, args.stage, args.output, args.psbc)
    print(f"PS4 shader created: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
