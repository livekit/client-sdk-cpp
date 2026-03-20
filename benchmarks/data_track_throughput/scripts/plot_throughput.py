#!/usr/bin/env python3

import argparse
import csv
import math
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import colors
from matplotlib.ticker import FormatStrFormatter


SUMMARY_FILE = "throughput_summary.csv"
MESSAGES_FILE = "throughput_messages.csv"

INT_FIELDS = {
    "run_id",
    "packet_size_bytes",
    "messages_requested",
    "messages_attempted",
    "messages_enqueued",
    "messages_enqueue_failed",
    "messages_received",
    "messages_missed",
    "duplicate_messages",
    "attempted_bytes",
    "enqueued_bytes",
    "received_bytes",
    "first_send_time_us",
    "last_send_time_us",
    "first_arrival_time_us",
    "last_arrival_time_us",
    "sequence",
    "payload_bytes",
    "send_time_us",
    "arrival_time_us",
    "is_duplicate",
}

FLOAT_FIELDS = {
    "desired_rate_hz",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate throughput benchmark plots from data_track_throughput CSV output."
    )
    parser.add_argument(
        "path",
        help="Path to the benchmark output directory, throughput_summary.csv, or throughput_messages.csv",
    )
    parser.add_argument(
        "--output-dir",
        help="Directory to write plots into. Default: <input>/plots",
    )
    return parser.parse_args()


def resolve_paths(path_arg: str) -> Tuple[Path, Path, Path]:
    input_path = Path(path_arg).expanduser().resolve()
    if input_path.is_dir():
        csv_dir = input_path
    elif input_path.is_file():
        csv_dir = input_path.parent
    else:
        raise FileNotFoundError(f"Input path does not exist: {input_path}")

    summary_path = csv_dir / SUMMARY_FILE
    messages_path = csv_dir / MESSAGES_FILE
    if not summary_path.exists():
        raise FileNotFoundError(f"Missing required summary CSV: {summary_path}")
    return csv_dir, summary_path, messages_path


def convert_value(key: str, value: str):
    if value == "":
        return None
    if key in INT_FIELDS:
        return int(value)
    if key in FLOAT_FIELDS:
        return float(value)
    return value


def load_csv_rows(path: Path) -> List[Dict[str, object]]:
    with path.open(newline="") as handle:
        reader = csv.DictReader(handle)
        return [{key: convert_value(key, value) for key, value in row.items()} for row in reader]


def payload_label(payload_bytes: int) -> str:
    units = ["B", "KiB", "MiB", "GiB"]
    value = float(payload_bytes)
    unit_index = 0
    while value >= 1024.0 and unit_index + 1 < len(units):
        value /= 1024.0
        unit_index += 1
    formatted = f"{value:g}"
    return f"{formatted} {units[unit_index]}"


def rate_label(rate_hz: float) -> str:
    if rate_hz >= 1000.0:
        value = rate_hz / 1000.0
        return f"{value:g}k Hz"
    return f"{rate_hz:g} Hz"


def throughput_label(mbps: float) -> str:
    if mbps >= 1000.0:
        return f"{mbps / 1000.0:.2f} Gbps"
    if mbps >= 100.0:
        return f"{mbps:.0f} Mbps"
    if mbps >= 10.0:
        return f"{mbps:.1f} Mbps"
    return f"{mbps:.2f} Mbps"


def save_plot(fig: plt.Figure, output_dir: Path, filename: str) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    fig.tight_layout()
    fig.savefig(output_dir / filename, dpi=180, bbox_inches="tight")
    plt.close(fig)


# ---------------------------------------------------------------------------
# Derived-value helpers (computed from raw CSV fields at analysis time)
# ---------------------------------------------------------------------------

def safe_duration_s(first_us: int, last_us: int) -> float:
    if first_us == 0 or last_us <= first_us:
        return 0.0
    return (last_us - first_us) / 1_000_000.0


def throughput_mbps(total_bytes: int, duration_s: float) -> float:
    if duration_s <= 0.0:
        return 0.0
    return (total_bytes * 8.0) / duration_s / 1_000_000.0


def percentile(values: List[float], p: float) -> float:
    if not values:
        return 0.0
    s = sorted(values)
    idx = (p / 100.0) * (len(s) - 1)
    lower = int(math.floor(idx))
    upper = int(math.ceil(idx))
    if lower == upper:
        return s[lower]
    frac = idx - lower
    return s[lower] + (s[upper] - s[lower]) * frac


def enrich_summary_row(row: Dict[str, object]) -> Dict[str, object]:
    """Add derived fields to a summary row from its raw fields."""
    msg_req = row["messages_requested"]
    rate_hz = row["desired_rate_hz"]
    expected_duration = msg_req / rate_hz if rate_hz > 0 else 0.0

    send_duration = safe_duration_s(row["first_send_time_us"], row["last_send_time_us"])
    receive_window = safe_duration_s(row["first_arrival_time_us"], row["last_arrival_time_us"])

    row["expected_duration_s"] = expected_duration
    row["send_duration_s"] = send_duration
    row["receive_window_s"] = receive_window
    row["delivery_ratio"] = row["messages_received"] / msg_req if msg_req > 0 else 0.0
    row["expected_throughput_mbps"] = throughput_mbps(row["attempted_bytes"], expected_duration)
    row["enqueue_throughput_mbps"] = throughput_mbps(row["enqueued_bytes"], send_duration)
    row["actual_throughput_mbps"] = throughput_mbps(row["received_bytes"], receive_window)
    received = row["messages_received"]
    row["average_receive_rate_hz"] = (received - 1) / receive_window if received > 1 and receive_window > 0 else 0.0
    return row


def enrich_message_rows(rows: Sequence[Dict[str, object]]) -> List[Dict[str, object]]:
    """Add latency_ms and interarrival_ms to per-message rows (grouped by run_id)."""
    sorted_rows = sorted(rows, key=lambda r: (r["run_id"], r["arrival_time_us"]))
    prev_arrival_by_run: Dict[int, int] = {}
    enriched = []
    for row in sorted_rows:
        rid = row["run_id"]
        send_us = row["send_time_us"]
        arrival_us = row["arrival_time_us"]
        row["latency_ms"] = (arrival_us - send_us) / 1000.0 if send_us > 0 and arrival_us >= send_us else None
        prev = prev_arrival_by_run.get(rid)
        row["interarrival_ms"] = (arrival_us - prev) / 1000.0 if prev is not None and arrival_us >= prev else None
        prev_arrival_by_run[rid] = arrival_us
        enriched.append(row)
    return enriched


def compute_latency_stats(message_rows: Sequence[Dict[str, object]]) -> Dict[int, Dict[str, float]]:
    """Compute per-run latency statistics from per-message rows."""
    by_run: Dict[int, List[float]] = {}
    for row in message_rows:
        if row.get("is_duplicate", 0) == 1:
            continue
        lat = row.get("latency_ms")
        if lat is not None:
            by_run.setdefault(row["run_id"], []).append(lat)

    stats: Dict[int, Dict[str, float]] = {}
    for rid, lats in by_run.items():
        stats[rid] = {
            "average_latency_ms": sum(lats) / len(lats) if lats else 0.0,
            "p50_latency_ms": percentile(lats, 50.0),
            "p95_latency_ms": percentile(lats, 95.0),
            "max_latency_ms": max(lats) if lats else 0.0,
        }
    return stats


# ---------------------------------------------------------------------------
# Plots
# ---------------------------------------------------------------------------

def create_expected_vs_actual_plot(summary_rows: Sequence[Dict[str, object]], output_dir: Path) -> None:
    fig, ax = plt.subplots(figsize=(9, 6))
    expected = [row["expected_throughput_mbps"] for row in summary_rows]
    actual = [row["actual_throughput_mbps"] for row in summary_rows]
    rates = [row["desired_rate_hz"] for row in summary_rows]
    sizes = [row["packet_size_bytes"] for row in summary_rows]
    point_sizes = [40 + (math.log10(max(1, size)) * 14) for size in sizes]

    scatter = ax.scatter(expected, actual, c=rates, s=point_sizes, cmap="viridis", alpha=0.85)
    max_value = max(expected + actual) if expected or actual else 1.0
    ax.plot([0, max_value], [0, max_value], linestyle="--", color="gray", linewidth=1.0, label="ideal")
    use_gbps = max_value >= 1000.0
    if use_gbps:
        ax.xaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v / 1000:.1f}"))
        ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v / 1000:.1f}"))
    ax.set_title("Expected vs Actual Throughput")
    unit = "Gbps" if use_gbps else "Mbps"
    ax.set_xlabel(f"Expected throughput ({unit})")
    ax.set_ylabel(f"Actual receive throughput ({unit})")
    ax.grid(alpha=0.25)
    ax.legend()
    colorbar = fig.colorbar(scatter, ax=ax)
    colorbar.set_label("Desired rate (Hz)")
    tick_rates = sorted({r for r in rates})
    if len(tick_rates) > 8:
        tick_rates = tick_rates[::max(1, len(tick_rates) // 8)]
    colorbar.set_ticks(tick_rates)
    colorbar.set_ticklabels([rate_label(r) for r in tick_rates])
    save_plot(fig, output_dir, "expected_vs_actual_throughput.png")


def create_drops_vs_throughput_plot(summary_rows: Sequence[Dict[str, object]], output_dir: Path) -> None:
    fig, ax = plt.subplots(figsize=(9, 6))
    x_values = [row["expected_throughput_mbps"] for row in summary_rows]
    y_values = [row["messages_missed"] for row in summary_rows]
    colors_data = [row["packet_size_bytes"] for row in summary_rows]

    vmin = max(1, min(colors_data))
    vmax = max(colors_data) if colors_data else 1
    scatter = ax.scatter(
        x_values,
        y_values,
        c=colors_data,
        cmap="plasma",
        norm=colors.LogNorm(vmin=vmin, vmax=vmax),
        s=85,
        alpha=0.85,
    )
    max_expected = max(x_values) if x_values else 1.0
    use_gbps = max_expected >= 1000.0
    if use_gbps:
        ax.xaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v / 1000:.1f}"))
    ax.set_title("Dropped Messages vs Expected Throughput")
    unit = "Gbps" if use_gbps else "Mbps"
    ax.set_xlabel(f"Expected throughput ({unit})")
    ax.set_ylabel("Dropped / missed messages")
    ax.grid(alpha=0.25)
    colorbar = fig.colorbar(scatter, ax=ax)
    colorbar.set_label("Payload size (bytes)")
    tick_values = sorted({v for v in colors_data if vmin <= v <= vmax})
    if len(tick_values) > 10:
        tick_values = tick_values[::max(1, len(tick_values) // 8)]
    colorbar.set_ticks(tick_values)
    colorbar.set_ticklabels([payload_label(int(v)) for v in tick_values])
    save_plot(fig, output_dir, "dropped_messages_vs_expected_throughput.png")


def build_heatmap(
    rows: Sequence[Dict[str, object]],
    value_key: str,
    title: str,
    colorbar_label: str,
    filename: str,
    output_dir: Path,
    annotation_fn=None,
    colorbar_fmt: Optional[str] = None,
) -> None:
    if not rows:
        return

    rates = sorted({row["desired_rate_hz"] for row in rows})
    payloads = sorted({row["packet_size_bytes"] for row in rows})
    grid = [[math.nan for _ in rates] for _ in payloads]

    grouped: Dict[Tuple[int, float], List[float]] = {}
    for row in rows:
        key = (row["packet_size_bytes"], row["desired_rate_hz"])
        grouped.setdefault(key, []).append(float(row[value_key]))

    for payload_idx, payload in enumerate(payloads):
        for rate_idx, rate in enumerate(rates):
            values = grouped.get((payload, rate))
            if values:
                grid[payload_idx][rate_idx] = sum(values) / len(values)

    fig, ax = plt.subplots(figsize=(10, 6))
    image = ax.imshow(grid, aspect="auto", origin="lower", cmap="viridis")
    ax.set_title(title)
    ax.set_xlabel("Desired rate (Hz)")
    ax.set_ylabel("Payload size")
    ax.set_xticks(range(len(rates)))
    ax.set_xticklabels([rate_label(rate) for rate in rates], rotation=45, ha="right")
    ax.set_yticks(range(len(payloads)))
    ax.set_yticklabels([payload_label(payload) for payload in payloads])

    for payload_idx, payload in enumerate(payloads):
        for rate_idx, rate in enumerate(rates):
            value = grid[payload_idx][rate_idx]
            if math.isnan(value):
                continue
            label = annotation_fn(value) if annotation_fn else f"{value:.1f}"
            ax.text(rate_idx, payload_idx, label, ha="center", va="center", color="white", fontsize=7)

    colorbar = fig.colorbar(image, ax=ax)
    colorbar.set_label(colorbar_label)
    if colorbar_fmt is not None:
        colorbar.ax.yaxis.set_major_formatter(FormatStrFormatter(colorbar_fmt))
    save_plot(fig, output_dir, filename)


def create_delivery_ratio_plot(summary_rows: Sequence[Dict[str, object]], output_dir: Path) -> None:
    build_heatmap(
        summary_rows,
        "delivery_ratio",
        "Delivery Ratio Heatmap",
        "Delivery ratio",
        "delivery_ratio_heatmap.png",
        output_dir,
        annotation_fn=lambda v: f"{v:.2f}",
        colorbar_fmt="%.2f",
    )


def create_actual_throughput_plot(summary_rows: Sequence[Dict[str, object]], output_dir: Path) -> None:
    build_heatmap(
        summary_rows,
        "actual_throughput_mbps",
        "Actual Throughput Heatmap",
        "Actual receive throughput (Mbps)",
        "actual_throughput_heatmap.png",
        output_dir,
        annotation_fn=lambda v: throughput_label(v),
    )


def create_latency_heatmap(
    summary_rows: Sequence[Dict[str, object]],
    latency_stats: Dict[int, Dict[str, float]],
    output_dir: Path,
) -> None:
    rows_with_lat = []
    for row in summary_rows:
        stats = latency_stats.get(row["run_id"])
        if stats:
            rows_with_lat.append({**row, **stats})
    build_heatmap(
        rows_with_lat,
        "p95_latency_ms",
        "P95 Latency Heatmap",
        "P95 latency (ms)",
        "p95_latency_heatmap.png",
        output_dir,
    )


def create_median_latency_heatmap(
    summary_rows: Sequence[Dict[str, object]],
    latency_stats: Dict[int, Dict[str, float]],
    output_dir: Path,
) -> None:
    rows_with_lat = []
    for row in summary_rows:
        stats = latency_stats.get(row["run_id"])
        if stats:
            rows_with_lat.append({**row, **stats})
    build_heatmap(
        rows_with_lat,
        "p50_latency_ms",
        "Median Latency Heatmap",
        "Median latency (ms)",
        "p50_latency_heatmap.png",
        output_dir,
    )


def create_message_latency_histogram(message_rows: Sequence[Dict[str, object]], output_dir: Path) -> None:
    latencies = [row["latency_ms"] for row in message_rows if row.get("latency_ms") is not None and row.get("is_duplicate", 0) != 1]
    if not latencies:
        return

    fig, ax = plt.subplots(figsize=(9, 6))
    ax.hist(latencies, bins=min(60, max(10, int(math.sqrt(len(latencies))))), color="#3c7dd9", alpha=0.9)
    ax.set_title("Message Latency Distribution")
    ax.set_xlabel("Latency (ms)")
    ax.set_ylabel("Message count")
    ax.grid(alpha=0.2)
    save_plot(fig, output_dir, "message_latency_histogram.png")


def create_message_interarrival_plot(message_rows: Sequence[Dict[str, object]], output_dir: Path) -> None:
    rows = [row for row in message_rows if row.get("interarrival_ms") is not None and row.get("is_duplicate", 0) != 1]
    if not rows:
        return

    rows = sorted(rows, key=lambda row: (row["run_id"], row["arrival_time_us"]))
    fig, ax = plt.subplots(figsize=(10, 6))
    x_values = list(range(len(rows)))
    y_values = [row["interarrival_ms"] for row in rows]
    ax.plot(x_values, y_values, linewidth=1.0, alpha=0.9)
    ax.set_title("Inter-arrival Time Across Received Messages")
    ax.set_xlabel("Received message index")
    ax.set_ylabel("Inter-arrival time (ms)")
    ax.grid(alpha=0.2)
    save_plot(fig, output_dir, "message_interarrival_series.png")


def generate_plots(
    summary_rows: Sequence[Dict[str, object]],
    message_rows: Sequence[Dict[str, object]],
    latency_stats: Dict[int, Dict[str, float]],
    output_dir: Path,
) -> None:
    create_expected_vs_actual_plot(summary_rows, output_dir)
    create_drops_vs_throughput_plot(summary_rows, output_dir)
    create_actual_throughput_plot(summary_rows, output_dir)
    create_delivery_ratio_plot(summary_rows, output_dir)
    create_latency_heatmap(summary_rows, latency_stats, output_dir)
    create_median_latency_heatmap(summary_rows, latency_stats, output_dir)
    if message_rows:
        create_message_latency_histogram(message_rows, output_dir)
        create_message_interarrival_plot(message_rows, output_dir)


def main() -> int:
    args = parse_args()
    csv_dir, summary_path, messages_path = resolve_paths(args.path)
    output_dir = Path(args.output_dir).expanduser().resolve() if args.output_dir else csv_dir / "plots"

    summary_rows = load_csv_rows(summary_path)
    if not summary_rows:
        raise SystemExit(f"No rows found in {summary_path}")
    summary_rows = [enrich_summary_row(row) for row in summary_rows]

    message_rows = load_csv_rows(messages_path) if messages_path.exists() else []
    message_rows = enrich_message_rows(message_rows) if message_rows else []
    latency_stats = compute_latency_stats(message_rows) if message_rows else {}

    generate_plots(summary_rows, message_rows, latency_stats, output_dir)

    print(f"Wrote plots to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
