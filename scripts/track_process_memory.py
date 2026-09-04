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
import subprocess
import sys
import time


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
        "command",
        nargs=argparse.REMAINDER,
        help="command and its arguments; prefix it with -- when needed",
    )
    args = parser.parse_args()
    if args.interval <= 0:
        parser.error("--interval must be greater than zero")
    if not args.command:
        parser.error("a command is required")
    return args


def process_rss_kib(pid: int) -> int | None:
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
    else:
        assert final_rss_kib is not None
        assert peak_rss_kib is not None
        print(f"RSS initial: {format_rss(initial_rss_kib)}")
        print(f"RSS final observed: {format_rss(final_rss_kib)}")
        print(f"RSS peak: {format_rss(peak_rss_kib)}")
        print(f"RSS change: {format_rss(final_rss_kib - initial_rss_kib)}")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
