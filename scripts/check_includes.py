#!/usr/bin/env python3
"""Include-level layer check (docs/design/phase0-foundations.md §2).

CMake link rules cannot stop `#include "core/render/..."`, because every target shares the src/
include root. This script applies the same rules to includes, using the anno_layers.json that
cmake/AnnoLayers.cmake writes at configure time:

  R1  a file includes a header from a higher layer
  R2  a layer-5 file (tools, macros, features) includes outside its allowlist
  R3  core/types includes another target's header
  R4  a file below layer 6 includes core/io
  detail  a file includes another target's detail/ header
  name    restricted functions used outside their allowed folders (ADR-0003)

Exit code 0 when clean, 1 when any violation is found (all are printed).
"""

import argparse
import json
import re
import sys
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp", ".cc"}
TOP_LEVEL_FOLDERS = {"core", "tools", "macros", "features", "project", "host", "ui", "cli"}

# Restricted names and the folders allowed to use them (ADR-0003).
RESTRICTED_NAMES = {
    "unchecked_from_origin": ("core/types", "core/timemap"),
    "unchecked_since_origin": ("core/types", "core/timemap"),
    "to_serialized_seconds": ("core/types", "project", "cli"),
    "from_serialized_seconds": ("core/types", "project", "cli"),
}


def owning_folder(rel_dir: str, folders: dict) -> str | None:
    """Longest target folder that is rel_dir or a parent of it."""
    best = None
    for folder in folders:
        if rel_dir == folder or rel_dir.startswith(folder + "/"):
            if best is None or len(folder) > len(best):
                best = folder
    return best


def folder_allowed(folder: str, allowed: tuple) -> bool:
    return any(folder == a or folder.startswith(a + "/") for a in allowed)


def layer5_allows(cfg: dict, own: str, dep: str, dep_layer: int) -> bool:
    if dep_layer == 5 or dep in cfg["layer5_allowed_folders"]:
        return True
    return dep.startswith("core/analysis/") and own in cfg["analysis_exception_folders"]


def check_file(path: Path, rel: str, own: str, cfg: dict) -> list[str]:
    folders = cfg["folders"]
    own_layer = folders[own]["layer"]
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    for match in INCLUDE_RE.finditer(text):
        inc = match.group(1)
        parts = inc.split("/")
        if parts[0] not in TOP_LEVEL_FOLDERS:
            continue  # standard library or third-party
        dep = owning_folder("/".join(parts[:-1]), folders)
        where = f"{rel}: #include \"{inc}\""
        if dep is None:
            problems.append(f"{where}: unknown target folder (not in anno_layers.json)")
            continue
        if dep == own:
            continue
        dep_layer = folders[dep]["layer"]
        if "/detail/" in inc[len(dep):]:
            problems.append(f"{where}: detail header of another target ({dep})")
        if dep_layer > own_layer:
            problems.append(f"{where}: R1: {own} (layer {own_layer}) includes {dep} (layer {dep_layer})")
        if own_layer == 5 and not layer5_allows(cfg, own, dep, dep_layer):
            problems.append(f"{where}: R2: {own} (layer 5) may not use {dep}")
        if own == "core/types":
            problems.append(f"{where}: R3: core/types includes {dep}")
        if dep == cfg["io_folder"] and own_layer < cfg["io_min_layer"]:
            problems.append(f"{where}: R4: {own} (layer {own_layer}) includes core/io (ADR-0001)")

    for name, allowed in RESTRICTED_NAMES.items():
        if re.search(rf"\b{name}\b", text) and not folder_allowed(own, allowed):
            problems.append(f"{rel}: name: {name} used outside {', '.join(allowed)} (ADR-0003)")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--layers", required=True, type=Path, help="anno_layers.json from the build")
    parser.add_argument("--src", required=True, type=Path, help="the src/ directory to check")
    args = parser.parse_args()

    cfg = json.loads(args.layers.read_text(encoding="utf-8"))
    problems = []
    checked = 0
    for path in sorted(args.src.rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        rel = path.relative_to(args.src).as_posix()
        own = owning_folder(path.parent.relative_to(args.src).as_posix(), cfg["folders"])
        if own is None:
            problems.append(f"{rel}: file is not inside any target folder")
            continue
        checked += 1
        problems.extend(check_file(path, rel, own, cfg))

    for problem in problems:
        print(f"ANNO INCLUDE VIOLATION: {problem}")
    print(f"check_includes: {checked} files checked, {len(problems)} violation(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
