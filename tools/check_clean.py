#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that annokit stays a game-independent library.

Usage: python3 -I tools/check_clean.py [repo root]

Looks at every file git tracks (LICENSE aside) and fails when one
- has the extension of a game's data file or program (a library ships none),
- names a game, an edition, a publisher or a store, or a project built on annokit,
- speaks of "the original" (annokit implements formats; whose files they are is no concern here),
- cites a location in a program (a hex number of the form 0x004xxxxx to 0x006xxxxx), a command
  number ("op 0x4D") or a function of another program (CamelCase_CamelCase), or
- uses one of a few words about tooling that does not belong here.
Third-party code is checked for the names only (not for the years in them): its tables are full
of numbers of every shape.
Each hit is printed as path:line: reason. Exit code 1 when there are any.

There is no switch to silence the check; reword the line instead.
"""
import base64
import json
import pathlib
import re
import subprocess
import sys

SKIP_FILES = {"LICENSE", "tools/check_clean.py"}
THIRD_PARTY = "third_party/"

DATA_FILES = re.compile(r"\.(bsh|col|zei|cod|gad|scp|szs|szm|gam|smk|bik|bk2|exe|dll|hss|rda|bin|sav)$",
                        re.IGNORECASE)
ADDRESS = re.compile(r"\b0x00[4-6][0-9A-Fa-f]{5}\b")
FUNCTION_NAME = re.compile(r"\b(?!SDL_|SDLK_|LUA_|FetchContent_)[A-Z][a-z][A-Za-z]*_[A-Z][a-z][A-Za-z0-9]*\b")
OPCODE = re.compile(r"\bops? (?:0x[0-9A-Fa-f]+|\d+ sub\b)|\bsub 0x[0-9A-Fa-f]+", re.IGNORECASE)

# The terms are stored encoded so that this file does not match itself.
_TERMS = json.loads(base64.b64decode(
    "eyJuYW1lcyI6IFsiXFxiYW5ub1xcYiIsICJhbm5vID8xWzAtOV17M30iLCAib3BlbjFbMC05XXszfSIsICJcXGJvMVswLTld"
    "ezN9IiwgImhpc3RvcnlbIC1dZWRpdGlvbiIsICJrKD86XHUwMGY2fG9lfG8pbmlnc1sgLV0/ZWRpdGlvbiIsICJraW5nJz9z"
    "IGVkaXRpb24iLCAiZ29sZFsgLV1lZGl0aW9uIiwgInViaXNvZnQiLCAic3VuZmxvd2VycyIsICJtYXggP2Rlc2lnbiIsICJy"
    "ZWxhdGVkIGRlc2lnbnMiLCAiXFxiZ29nXFxiIiwgImFubm9bXy1dcmVidWlsZCJdLCAieWVhcnMiOiBbIlxcYjE2MDJcXGIi"
    "LCAiXFxiMTUwM1xcYiIsICJcXGIxNzAxXFxiIiwgIlxcYjE0MDRcXGIiLCAiXFxiMTgwMFxcYiJdLCAib3JpZ2luYWwiOiAi"
    "XFxib3JpZ2luYWwiLCAid29yZHMiOiBbInJldmVyc2VbLSBdP2VuZ2luZWVyIiwgImRpc2Fzc2VtYmwiLCAiZGVjb21waWwi"
    "LCAiZ2hpZHJhIiwgIlxcYmlkYSBwcm9cXGIiLCAiXFxieCg/OjMyfDY0KWRiZ1xcYiIsICJvbGx5ZGJnIiwgIlxcYnJlYS1h"
    "Z2VudHNcXGIiLCAiXFxibWF4d2F2ZSJdLCAiY2FzZV93b3JkcyI6IFsiXFxiSURBXFxiIl19"))
NAMES = re.compile("|".join(_TERMS["names"]), re.IGNORECASE)
ORIGINAL = re.compile(_TERMS["original"], re.IGNORECASE)
WORDS = re.compile("|".join(_TERMS["words"]), re.IGNORECASE)
CASE_WORDS = re.compile("|".join(_TERMS["case_words"]))
YEARS = re.compile("|".join(_TERMS["years"]))


def tracked_files(root: pathlib.Path) -> list[str]:
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True, check=True)
    return [p for p in out.stdout.decode("utf-8").split("\0") if p]


def check_line(line: str, third_party: bool) -> list[str]:
    reasons = []
    if NAMES.search(line):
        reasons.append("name of a game, edition, publisher or project")
    if third_party:
        return reasons
    if YEARS.search(line):
        reasons.append("year in a game's name")
    if ORIGINAL.search(line):
        reasons.append('"original"')
    if ADDRESS.search(line):
        reasons.append("location in a program")
    if FUNCTION_NAME.search(line):
        reasons.append(f"function name of another program ({', '.join(sorted(set(FUNCTION_NAME.findall(line))))})")
    if OPCODE.search(line):
        reasons.append("command number")
    if WORDS.search(line) or CASE_WORDS.search(line):
        reasons.append("term that does not belong in this repository")
    return reasons


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    problems = []
    for path in tracked_files(root):
        if path in SKIP_FILES:
            continue
        third_party = path.startswith(THIRD_PARTY)
        if NAMES.search(path):
            problems.append(f"{path}: name of a game, edition, publisher or project in the path")
        if not third_party and DATA_FILES.search(path):
            problems.append(f"{path}: data file or program of a game")
            continue
        full = root / path
        if not full.is_file():
            continue
        data = full.read_bytes()
        if b"\0" in data[:8192]:
            continue  # binary (test media)
        text = data.decode("utf-8", errors="replace")
        for number, line in enumerate(text.splitlines(), 1):
            for reason in check_line(line, third_party):
                problems.append(f"{path}:{number}: {reason}")
    for problem in problems:
        print(problem)
    if problems:
        print(f"{len(problems)} problem(s); see tools/check_clean.py")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
