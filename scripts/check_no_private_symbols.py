#!/usr/bin/env python3
# Copyright 2026 LiveKit
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
"""
Verify that liblivekit's exported ABI does not leak private dependency symbols.

The LiveKit SDK statically links several private dependencies (spdlog, fmt,
google::protobuf, absl).  When those symbols escape the dynamic symbol table
of liblivekit.{so,dylib,dll}, they collide at runtime with the same libraries
loaded elsewhere in the host process (a common failure mode is ROS 2's
rcl_logging_spdlog ABI-clashing with our vendored spdlog and crashing inside
spdlog::pattern_formatter).

This script lists exported defined symbols from the supplied shared library
using the platform-appropriate tool and fails (exit code 1) if any of them
match a forbidden pattern.

Usage:
    python3 check_no_private_symbols.py <path-to-shared-library>

Optional environment variables:
    LIVEKIT_SYMBOL_CHECK_VERBOSE=1   Print every leaked symbol (default: print
                                    up to 20 examples).
    LIVEKIT_SYMBOL_CHECK_EXTRA_FORBIDDEN=foo,bar   Additional comma-separated
                                                   patterns to forbid.
"""

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# Substring patterns that must NOT appear in any exported symbol after
# demangling.  We use plain-substring semantics for readability; if you need a
# regex, switch to re.search.
DEFAULT_FORBIDDEN = [
    "spdlog::",
    "fmt::v",
    "google::protobuf",
    "absl::",
]

MAX_REPORTED_LEAKS = 20


def _which_or_die(name: str) -> str:
    path = shutil.which(name)
    if not path:
        sys.stderr.write(f"error: required tool '{name}' not found on PATH\n")
        sys.exit(2)
    return path


def _list_exports_macos(lib: Path) -> list[str]:
    nm = _which_or_die("nm")
    cxxfilt = shutil.which("c++filt")
    # -gU: external (global) defined symbols.
    raw = subprocess.run(
        [nm, "-gU", str(lib)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    if cxxfilt:
        raw = subprocess.run(
            [cxxfilt],
            input=raw,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    return raw.splitlines()


def _list_exports_linux(lib: Path) -> list[str]:
    nm = _which_or_die("nm")
    cxxfilt = shutil.which("c++filt")
    # -D: dynamic symbols (i.e., what's actually visible to the dynamic linker)
    # --defined-only: drop UND entries
    raw = subprocess.run(
        [nm, "-D", "--defined-only", str(lib)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    if cxxfilt:
        raw = subprocess.run(
            [cxxfilt],
            input=raw,
            check=True,
            capture_output=True,
            text=True,
        ).stdout
    return raw.splitlines()


def _list_exports_windows(lib: Path) -> list[str]:
    # dumpbin ships with MSVC; it understands import libs (.lib) and DLLs.
    dumpbin = shutil.which("dumpbin")
    if not dumpbin:
        sys.stderr.write(
            "error: 'dumpbin' not on PATH; run from a Visual Studio "
            "Developer command prompt or ensure dumpbin.exe is available\n"
        )
        sys.exit(2)
    raw = subprocess.run(
        [dumpbin, "/exports", str(lib)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    # dumpbin output lines for export entries look like
    #   "          1    0 00001000 ?foo@@YAHXZ = ?foo@@YAHXZ (int __cdecl foo(void))"
    # We keep all of stdout: the substring search will only fire on actual
    # symbol names, headers/footers are harmless.
    return raw.splitlines()


def _list_exports(lib: Path) -> list[str]:
    if sys.platform == "darwin":
        return _list_exports_macos(lib)
    if sys.platform.startswith("linux"):
        return _list_exports_linux(lib)
    if os.name == "nt" or sys.platform == "win32":
        return _list_exports_windows(lib)
    sys.stderr.write(f"error: unsupported platform '{sys.platform}'\n")
    sys.exit(2)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        sys.stderr.write(__doc__ or "")
        return 2

    lib = Path(argv[1]).resolve()
    if not lib.exists():
        sys.stderr.write(f"error: library not found: {lib}\n")
        return 2

    forbidden = list(DEFAULT_FORBIDDEN)
    extra = os.environ.get("LIVEKIT_SYMBOL_CHECK_EXTRA_FORBIDDEN", "")
    if extra:
        forbidden.extend(p for p in extra.split(",") if p)

    verbose = bool(os.environ.get("LIVEKIT_SYMBOL_CHECK_VERBOSE"))

    print(f"[symbol-check] library : {lib}")
    print(f"[symbol-check] platform: {sys.platform}")
    print(f"[symbol-check] forbidden patterns: {forbidden}")

    lines = _list_exports(lib)
    print(f"[symbol-check] {len(lines)} lines of nm/dumpbin output")

    # Group leaks by pattern for a tidy summary.
    leaks_by_pattern: dict[str, list[str]] = {p: [] for p in forbidden}
    for line in lines:
        for pat in forbidden:
            if pat in line:
                leaks_by_pattern[pat].append(line.rstrip())

    total_leaks = sum(len(v) for v in leaks_by_pattern.values())
    if total_leaks == 0:
        print("[symbol-check] OK: no forbidden symbols exported")
        return 0

    print(f"[symbol-check] FAIL: {total_leaks} forbidden symbol(s) exported")
    for pat, hits in leaks_by_pattern.items():
        if not hits:
            continue
        print(f"  pattern {pat!r}: {len(hits)} hit(s)")
        shown = hits if verbose else hits[:MAX_REPORTED_LEAKS]
        for h in shown:
            print(f"    {h}")
        if not verbose and len(hits) > MAX_REPORTED_LEAKS:
            print(f"    ... and {len(hits) - MAX_REPORTED_LEAKS} more "
                  "(set LIVEKIT_SYMBOL_CHECK_VERBOSE=1 to see all)")

    print(
        "\nliblivekit must not re-export private dependency symbols.\n"
        "If you intentionally added a public symbol that triggered this, mark\n"
        "it with LIVEKIT_API in include/livekit/export.h and rebuild.\n"
    )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
