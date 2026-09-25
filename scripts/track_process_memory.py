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
import signal
import subprocess
import sys
import time
from collections.abc import Callable

MEMORY_LIMIT_ENV = "LIVEKIT_MEMORY_MAX_FINAL_RSS_KIB"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run a command and report its initial, final, peak, and per-interval RSS."
    )
    parser.add_argument(
        "--interval",
        type=float,
        default=1.0,
        help="seconds between live RSS prints (default: 1.0)",
    )
    parser.add_argument(
        "--warmup",
        type=float,
        default=1.0,
        help="seconds to wait before recording initial/peak/delta stats (default: 1.0)",
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
    if args.warmup < 0:
        parser.error("--warmup must be greater than or equal to zero")
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


def format_signed_rss(delta_kib: int) -> str:
    sign = "+" if delta_kib >= 0 else ""
    return f"{sign}{delta_kib:,} KiB ({sign}{delta_kib / 1024:.2f} MiB)"


def print_live_sample(
    elapsed_s: float,
    rss_kib: int | None,
    delta_kib: int | None,
    peak_rss_kib: int | None,
    warmup: bool,
) -> None:
    prefix = "warmup " if warmup else ""
    if rss_kib is None:
        print(f"[{elapsed_s:6.1f}s] {prefix}RSS unavailable", flush=True)
        return
    parts = [f"[{elapsed_s:6.1f}s] {prefix}RSS {format_rss(rss_kib)}"]
    if delta_kib is not None:
        parts.append(f"Δ {format_signed_rss(delta_kib)}")
    if peak_rss_kib is not None:
        parts.append(f"peak {format_rss(peak_rss_kib)}")
    print("  ".join(parts), flush=True)


def wait_for_exit_or_interval(
    process: subprocess.Popen,
    interval_s: float,
    should_stop: Callable[[], bool] | None = None,
) -> None:
    deadline = time.monotonic() + interval_s
    while process.poll() is None:
        if should_stop is not None and should_stop():
            return
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return
        time.sleep(min(0.05, remaining))


def spawn_monitored_process(command: list[str]) -> subprocess.Popen:
    kwargs: dict[str, object] = {}
    if sys.platform == "win32":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP
    else:
        kwargs["start_new_session"] = True
    return subprocess.Popen(command, **kwargs)


def stop_monitored_process(process: subprocess.Popen, *, timeout_s: float = 5.0) -> None:
    if process.poll() is not None:
        return

    def send(posix_signal: int, windows_signal: int | None = None) -> None:
        try:
            if sys.platform == "win32":
                process.send_signal(windows_signal if windows_signal is not None else signal.SIGTERM)
            else:
                os.killpg(os.getpgid(process.pid), posix_signal)
        except (OSError, ProcessLookupError):
            return

    if sys.platform == "win32":
        send(signal.SIGTERM, signal.CTRL_BREAK_EVENT)
    else:
        send(signal.SIGINT)
    try:
        process.wait(timeout=timeout_s)
        return
    except subprocess.TimeoutExpired:
        pass

    send(signal.SIGTERM)
    try:
        process.wait(timeout=2)
        return
    except subprocess.TimeoutExpired:
        pass

    if sys.platform == "win32":
        process.kill()
    else:
        send(signal.SIGKILL)
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


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
        process = spawn_monitored_process(command)
    except OSError as error:
        print(f"error: could not start {command[0]!r}: {error}", file=sys.stderr)
        return 127

    stop_requested = False

    def handle_stop_signal(_signum: int, _frame: object) -> None:
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, handle_stop_signal)
    if hasattr(signal, "SIGTERM"):
        signal.signal(signal.SIGTERM, handle_stop_signal)

    started_at = time.monotonic()
    last_rss_kib: int | None = None
    last_stats_rss_kib: int | None = None
    initial_rss_kib: int | None = None
    final_rss_kib: int | None = None
    peak_rss_kib: int | None = None
    interval_deltas_kib: list[int] = []

    try:
        while process.poll() is None and not stop_requested:
            elapsed_s = time.monotonic() - started_at
            warmed_up = elapsed_s >= args.warmup
            rss_kib = process_rss_kib(process.pid)
            delta_kib: int | None = None
            if rss_kib is not None:
                if last_rss_kib is not None:
                    delta_kib = rss_kib - last_rss_kib
                last_rss_kib = rss_kib
                if warmed_up:
                    if initial_rss_kib is None:
                        initial_rss_kib = rss_kib
                    else:
                        assert last_stats_rss_kib is not None
                        interval_deltas_kib.append(rss_kib - last_stats_rss_kib)
                    last_stats_rss_kib = rss_kib
                    final_rss_kib = rss_kib
                    peak_rss_kib = max(peak_rss_kib or rss_kib, rss_kib)
            print_live_sample(elapsed_s, rss_kib, delta_kib, peak_rss_kib, warmup=not warmed_up)
            if process.poll() is not None or stop_requested:
                break
            wait_for_exit_or_interval(process, args.interval, lambda: stop_requested)
    except KeyboardInterrupt:
        stop_requested = True

    if stop_requested and process.poll() is None:
        print("interrupted; stopping monitored process", file=sys.stderr, flush=True)
        stop_monitored_process(process)

    elapsed_s = time.monotonic() - started_at
    exit_code = process.wait()
    print(f"command: {' '.join(command)}")
    print(f"exit code: {exit_code}")
    print(f"elapsed: {elapsed_s:.2f} s")
    if initial_rss_kib is None:
        if args.warmup > 0:
            print(
                f"RSS: no samples collected after {args.warmup:.1f}s warmup; "
                "the command exited too quickly"
            )
        else:
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
        warmup_note = f" (after {args.warmup:.1f}s warmup)" if args.warmup > 0 else ""
        print(f"RSS initial{warmup_note}: {format_rss(initial_rss_kib)}")
        print(f"RSS final observed: {format_rss(final_rss_kib)}")
        print(f"RSS peak{warmup_note}: {format_rss(peak_rss_kib)}")
        print(f"RSS change{warmup_note}: {format_signed_rss(final_rss_kib - initial_rss_kib)}")
        if interval_deltas_kib:
            average_delta_kib = sum(interval_deltas_kib) / len(interval_deltas_kib)
            print(
                f"RSS Δ avg{warmup_note}: {format_signed_rss(round(average_delta_kib))} "
                f"({len(interval_deltas_kib)} interval(s))"
            )
            print(f"RSS Δ max{warmup_note}: {format_signed_rss(max(interval_deltas_kib))}")
            print(f"RSS Δ min{warmup_note}: {format_signed_rss(min(interval_deltas_kib))}")
        else:
            print(f"RSS Δ{warmup_note}: no interval deltas after warmup")
        if max_final_rss_kib is not None and final_rss_kib > max_final_rss_kib:
            print(
                "error: final RSS "
                f"{format_rss(final_rss_kib)} exceeds limit {format_rss(max_final_rss_kib)}",
                file=sys.stderr,
            )
            if exit_code == 0:
                return 1

    return 130 if stop_requested else exit_code


if __name__ == "__main__":
    raise SystemExit(main())
