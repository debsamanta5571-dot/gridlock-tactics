#!/usr/bin/env python3
"""Generate or verify TacticsGameUnreal CppCoreStub_*.cpp shims from cpp_core/CMakeLists.txt."""

from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CMAKE = REPO / "cpp_core" / "CMakeLists.txt"
STUB_DIR = REPO / "TacticsGameUnreal 5.8" / "Source" / "TacticsCore" / "Private"
# cpp_core/src is already in PrivateIncludePaths (TacticsCore.Build.cs), so we use a short
# relative include instead of a fragile ../../../../ traversal.  The compiler resolves
# e.g. "board/tile_modifiers.cpp" directly against that search path.
PCH_INCLUDE = '#include "TacticsCorePrivatePCH.h"\n'
INCLUDE_LINE = '#include "{short}"\n'
# Must exceed TacticsCore.Build.cs NumIncludedBytesPerUnityCPPOverride so UBT keeps one stub per unity TU.
STUB_UNITY_PAD_TARGET_BYTES = 128


def parse_tactics_core_sources(cmake_text: str) -> list[str]:
    in_lib = False
    sources: list[str] = []
    for line in cmake_text.splitlines():
        if line.strip().startswith("add_library(tactics_core"):
            in_lib = True
            continue
        if in_lib and line.strip() == ")":
            break
        if in_lib:
            m = re.match(r"\s+src/(.+\.cpp)\s*$", line)
            if m:
                sources.append("src/" + m.group(1))
    return sources


def stub_path_for_source(rel: str) -> Path:
    name = Path(rel).name.replace(".cpp", "")
    return STUB_DIR / f"CppCoreStub_{name}.cpp"


def stub_contents(rel: str) -> str:
    # rel is e.g. "src/board/tile_modifiers.cpp"; strip the leading "src/" so the include
    # is relative to cpp_core/src (which TacticsCore.Build.cs adds to PrivateIncludePaths).
    short = rel.replace("\\", "/").removeprefix("src/")
    slug = Path(rel).stem
    lines = [
        "// Auto-synced with cpp_core/CMakeLists.txt - do not edit by hand; re-run scripts/generate_cpp_core_stubs.py",
        f"// Unity-build isolation ({slug}): one cpp_core translation unit per unity blob.",
        PCH_INCLUDE.rstrip("\n"),
        INCLUDE_LINE.format(short=short).rstrip("\n"),
    ]
    body = "\n".join(lines) + "\n"
    encoded = body.encode("utf-8")
    if len(encoded) < STUB_UNITY_PAD_TARGET_BYTES:
        pad_len = STUB_UNITY_PAD_TARGET_BYTES - len(encoded)
        body += "//" + ("-" * max(0, pad_len - 3)) + "\n"
    return body


def main() -> int:
    if not CMAKE.is_file():
        print(f"Missing {CMAKE}", file=sys.stderr)
        return 1
    sources = parse_tactics_core_sources(CMAKE.read_text(encoding="utf-8"))
    if not sources:
        print("No tactics_core sources found in CMakeLists.txt", file=sys.stderr)
        return 1

    check_only = "--check" in sys.argv
    missing = []
    stale = []

    for rel in sources:
        stub = stub_path_for_source(rel)
        expected = stub_contents(rel)
        if not stub.is_file():
            missing.append((stub, expected))
            continue
        if stub.read_text(encoding="utf-8") != expected:
            stale.append((stub, expected))

    extra = sorted(
        p
        for p in STUB_DIR.glob("CppCoreStub_*.cpp")
        if p.name not in {stub_path_for_source(s).name for s in sources}
    )

    if check_only:
        ok = True
        if missing:
            ok = False
            print("Missing stubs:")
            for path, _ in missing:
                print(f"  {path.relative_to(REPO)}")
        if stale:
            ok = False
            print("Out of date stubs:")
            for path, _ in stale:
                print(f"  {path.relative_to(REPO)}")
        if extra:
            ok = False
            print("Extra stubs (not in tactics_core):")
            for path in extra:
                print(f"  {path.relative_to(REPO)}")
        if ok:
            print(f"OK: {len(sources)} stubs match CMakeLists.txt")
        return 0 if ok else 1

    STUB_DIR.mkdir(parents=True, exist_ok=True)
    for stub, content in missing + stale:
        stub.write_text(content, encoding="utf-8", newline="\n")
        print(f"Wrote {stub.relative_to(REPO)}")
    if extra:
        print("Warning: extra stubs (not removed automatically):")
        for path in extra:
            print(f"  {path.relative_to(REPO)}")
    print(f"Done. {len(sources)} tactics_core sources.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
