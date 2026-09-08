#!/usr/bin/env python3
#
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

"""Run a command and report its resident memory usage."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

MEMORY_LIMIT_ENV = "LIVEKIT_MEMORY_MAX_FINAL_RSS_KIB"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a command and report its initial, final, and peak RSS."
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=0.1,
        help="seconds between RSS samples (default: 0.1)",
    )
    parser.add_argument(
        "--max-final-rss-kib",
        type=int,
        default=None,
        help=(
            "fail if the last observed RSS exceeds this many KiB; "
            f"overrides {MEMORY_LIMIT_ENV} when set"
        ),
    )
    parser.add_argument(
        "command",
        nargs=argparse.REMAINDER,
        help="command and its arguments; prefix it with -- when needed",
    )
    args = parser.parse_args()
    if args.interval <= 0:
        parser.error("--interval must be greater than zero")
    if args.max_final_rss_kib is not None and args.max_final_rss_kib <= 0:
        parser.error("--max-final-rss-kib must be greater than zero")
    if not args.command:
        parser.error("a command is required")
    return args


def resolve_max_final_rss_kib(explicit: int | None) -> int | None:
    if explicit is not None:
        return explicit
    value = os.environ.get(MEMORY_LIMIT_ENV)
    if value is None or value == "":
        return None
    try:
        parsed = int(value)
    except ValueError as error:
        raise ValueError(f"{MEMORY_LIMIT_ENV} must be an integer") from error
    if parsed <= 0:
        raise ValueError(f"{MEMORY_LIMIT_ENV} must be greater than zero")
    return parsed


def windows_rss_kib(pid: int) -> int | None:
    import ctypes
    from ctypes import wintypes

    class ProcessMemoryCounters(ctypes.Structure):
        _fields_ = [
            ("cb", wintypes.DWORD),
            ("PageFaultCount", wintypes.DWORD),
            ("PeakWorkingSetSize", ctypes.c_size_t),
            ("WorkingSetSize", ctypes.c_size_t),
            ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPagedPoolUsage", ctypes.c_size_t),
            ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
            ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
            ("PagefileUsage", ctypes.c_size_t),
            ("PeakPagefileUsage", ctypes.c_size_t),
        ]

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    psapi = ctypes.WinDLL("psapi", use_last_error=True)
    process_query_information = 0x0400
    process_vm_read = 0x0010
    handle = kernel32.OpenProcess(process_query_information | process_vm_read, False, pid)
    if not handle:
        return None
    try:
        counters = ProcessMemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            return None
        rss_kib = int(counters.WorkingSetSize) // 1024
        return rss_kib if rss_kib > 0 else None
    finally:
        kernel32.CloseHandle(handle)


def process_rss_kib(pid: int) -> int | None:
    if sys.platform == "win32":
        return windows_rss_kib(pid)

    result = subprocess.run(
        ["ps", "-o", "rss=", "-p", str(pid)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None

    rss = result.stdout.strip()
    if not rss:
        return None

    rss_kib = int(rss)
    # macOS reports 0 RSS for a child that has exited but has not yet been
    # reaped. Do not overwrite the last live-process sample with that value.
    return rss_kib if rss_kib > 0 else None


def format_rss(rss_kib: int) -> str:
    return f"{rss_kib:,} KiB ({rss_kib / 1024:.2f} MiB)"


def main() -> int:
    args = parse_args()
    command = args.command
    if command[0] == "--":
        command = command[1:]
    if not command:
        print("error: a command is required after --", file=sys.stderr)
        return 2

    try:
        max_final_rss_kib = resolve_max_final_rss_kib(args.max_final_rss_kib)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    try:
        process = subprocess.Popen(command)
    except OSError as error:
        print(f"error: could not start {command[0]!r}: {error}", file=sys.stderr)
        return 127

    started_at = time.monotonic()
    initial_rss_kib: int | None = None
    final_rss_kib: int | None = None
    peak_rss_kib: int | None = None

    while process.poll() is None:
        rss_kib = process_rss_kib(process.pid)
        if rss_kib is not None:
            if initial_rss_kib is None:
                initial_rss_kib = rss_kib
            final_rss_kib = rss_kib
            peak_rss_kib = max(peak_rss_kib or rss_kib, rss_kib)
        time.sleep(args.interval)

    elapsed_s = time.monotonic() - started_at
    exit_code = process.wait()
    print(f"command: {' '.join(command)}")
    print(f"exit code: {exit_code}")
    print(f"elapsed: {elapsed_s:.2f} s")
    if initial_rss_kib is None:
        print("RSS: no samples collected; the command exited before sampling began")
        if max_final_rss_kib is not None and exit_code == 0:
            print(
                f"error: {MEMORY_LIMIT_ENV} is set but RSS could not be sampled",
                file=sys.stderr,
            )
            return 1
    else:
        assert final_rss_kib is not None
        assert peak_rss_kib is not None
        print(f"RSS initial: {format_rss(initial_rss_kib)}")
        print(f"RSS final observed: {format_rss(final_rss_kib)}")
        print(f"RSS peak: {format_rss(peak_rss_kib)}")
        print(f"RSS change: {format_rss(final_rss_kib - initial_rss_kib)}")
        if max_final_rss_kib is not None and final_rss_kib > max_final_rss_kib:
            print(
                "error: final RSS "
                f"{format_rss(final_rss_kib)} exceeds limit {format_rss(max_final_rss_kib)}",
                file=sys.stderr,
            )
            if exit_code == 0:
                return 1

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
