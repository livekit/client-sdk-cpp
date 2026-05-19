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

"""Track memory usage for one process by executable name."""

from __future__ import annotations

import argparse
import datetime as dt
import os
import shlex
import subprocess
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Sample RSS memory for one process matching an executable name."
    )
    parser.add_argument(
        "process_name",
        nargs="?",
        default="livekit_unit_tests",
        help="executable name to match exactly (default: livekit_unit_tests)",
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="seconds between samples (default: 1.0)",
    )
    parser.add_argument(
        "--wait",
        action="store_true",
        help="wait for a matching process instead of exiting when none is found",
    )
    return parser.parse_args()


def command_executable_name(command: str) -> str:
    try:
        argv = shlex.split(command)
    except ValueError:
        argv = command.split()

    if not argv:
        return ""
    return os.path.basename(argv[0])


def matching_processes(process_name: str) -> list[tuple[int, int, str]]:
    output = subprocess.check_output(["ps", "-axo", "pid=,rss=,command="], text=True)
    matches: list[tuple[int, int, str]] = []
    own_pid = str(os.getpid())

    for line in output.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) != 3:
            continue

        pid, rss_kib, command = parts
        if pid == own_pid or command_executable_name(command) != process_name:
            continue

        matches.append((int(pid), int(rss_kib), command))

    return matches


def process_rss(pid: int) -> int | None:
    try:
        output = subprocess.check_output(["ps", "-o", "rss=", "-p", str(pid)], text=True)
    except subprocess.CalledProcessError:
        return None

    stripped = output.strip()
    if not stripped:
        return None
    return int(stripped)


def select_process(process_name: str, wait: bool, interval: float) -> tuple[int, int, str] | None:
    while True:
        matches = matching_processes(process_name)
        if matches:
            matches.sort(key=lambda match: match[0], reverse=True)
            selected = matches[0]
            if len(matches) > 1:
                other_pids = ", ".join(str(pid) for pid, _, _ in matches[1:])
                print(
                    f"multiple processes named {process_name!r}; tracking newest pid={selected[0]} "
                    f"(ignoring pid(s): {other_pids})",
                    file=sys.stderr,
                )
            return selected

        if not wait:
            print(f"no process named {process_name!r}", file=sys.stderr)
            return None

        time.sleep(interval)


def main() -> int:
    args = parse_args()
    selected = select_process(args.process_name, args.wait, args.interval)
    if selected is None:
        return 1

    pid, start_rss_kib, command = selected
    started_at = time.monotonic()
    print(f"tracking process_name={args.process_name!r} pid={pid} command={command!r}")

    while True:
        rss_kib = process_rss(pid)
        if rss_kib is None:
            print(f"process pid={pid} exited", file=sys.stderr)
            return 0

        timestamp = dt.datetime.now(dt.timezone.utc).isoformat()
        elapsed_s = time.monotonic() - started_at
        if start_rss_kib == 0:
            change_pct = 0.0
        else:
            change_pct = ((rss_kib - start_rss_kib) / start_rss_kib) * 100.0

        print(
            f"{timestamp} elapsed={elapsed_s:.1f}s pid={pid} "
            f"Memory: {rss_kib} KB Percent change: {change_pct:+.2f}%"
        )

        sys.stdout.flush()
        time.sleep(args.interval)


if __name__ == "__main__":
    raise SystemExit(main())
