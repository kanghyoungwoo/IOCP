"""
ChatLoadTester CSV Metrics Analyzer
Usage: python analyze_metrics.py <csv_dir_or_files...>

Examples:
  python analyze_metrics.py metrics/
  python analyze_metrics.py bot_metrics_1.csv bot_metrics_2.csv
"""

import sys
import os
import glob
import csv
from collections import defaultdict

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


def load_csv_files(paths):
    """CSV 파일들을 로드하여 시간대별로 합산"""
    aggregated = defaultdict(lambda: {
        "conn": 0, "conn_fail": 0, "disconn": 0,
        "send_pkt": 0, "recv_pkt": 0,
        "send_kb": 0.0, "recv_kb": 0.0,
        "total_conn": 0, "total_disconn": 0,
        "lat_min": float("inf"), "lat_max": 0.0,
        "lat_avg_sum": 0.0, "lat_p99_max": 0.0,
        "lat_samples": 0, "file_count": 0,
    })

    file_count = 0
    for path in paths:
        with open(path, "r", encoding="utf-8-sig") as f:
            reader = csv.DictReader(f)
            for row in reader:
                sec = int(row["elapsed_sec"])
                d = aggregated[sec]
                d["conn"] += int(row["conn/s"])
                d["conn_fail"] += int(row["conn_fail/s"])
                d["disconn"] += int(row["disconn/s"])
                d["send_pkt"] += int(row["send_pkt/s"])
                d["recv_pkt"] += int(row["recv_pkt/s"])
                d["send_kb"] += float(row["send_KB/s"])
                d["recv_kb"] += float(row["recv_KB/s"])
                d["total_conn"] += int(row["total_conn"])
                d["total_disconn"] += int(row["total_disconn"])

                lat_min = float(row["lat_min_ms"])
                lat_max = float(row["lat_max_ms"])
                lat_avg = float(row["lat_avg_ms"])
                lat_p99 = float(row["lat_p99_ms"])
                samples = int(row["lat_samples"])

                if samples > 0:
                    if lat_min < d["lat_min"]:
                        d["lat_min"] = lat_min
                    if lat_max > d["lat_max"]:
                        d["lat_max"] = lat_max
                    d["lat_avg_sum"] += lat_avg * samples
                    if lat_p99 > d["lat_p99_max"]:
                        d["lat_p99_max"] = lat_p99
                    d["lat_samples"] += samples

                d["file_count"] += 1
        file_count += 1

    print(f"Loaded {file_count} CSV files")
    return aggregated


def detect_anomalies(aggregated):
    """이상 구간 자동 감지"""
    anomalies = []
    sorted_secs = sorted(aggregated.keys())

    # 평균 latency, disconnect 계산 (ramp-up 구간 제외)
    stable_start = len(sorted_secs) // 4  # 앞 25% 제외
    stable_secs = sorted_secs[stable_start:]

    if not stable_secs:
        return anomalies

    avg_lat_values = []
    disconn_values = []
    for sec in stable_secs:
        d = aggregated[sec]
        if d["lat_samples"] > 0:
            avg_lat_values.append(d["lat_avg_sum"] / d["lat_samples"])
        disconn_values.append(d["disconn"])

    if not avg_lat_values:
        return anomalies

    baseline_lat = sum(avg_lat_values) / len(avg_lat_values)
    baseline_disconn = sum(disconn_values) / len(disconn_values)

    for sec in sorted_secs:
        d = aggregated[sec]
        reasons = []

        # Latency 스파이크: 평균의 3배 이상
        if d["lat_samples"] > 0:
            avg_lat = d["lat_avg_sum"] / d["lat_samples"]
            if baseline_lat > 0 and avg_lat > baseline_lat * 3:
                reasons.append(f"latency spike {avg_lat:.1f}ms (baseline: {baseline_lat:.1f}ms)")

        # p99 > 200ms
        if d["lat_p99_max"] > 200:
            reasons.append(f"p99 = {d['lat_p99_max']:.1f}ms")

        # Disconnect 급증: 기준치의 5배 이상 또는 절대값 50 이상
        if d["disconn"] > max(baseline_disconn * 5, 50):
            reasons.append(f"disconnect surge: {d['disconn']}")

        # 연결 실패 급증
        if d["conn_fail"] > 10:
            reasons.append(f"connect failures: {d['conn_fail']}")

        if reasons:
            anomalies.append((sec, reasons))

    return anomalies


def print_summary(aggregated):
    """전체 테스트 요약 리포트"""
    sorted_secs = sorted(aggregated.keys())
    if not sorted_secs:
        print("No data found.")
        return

    duration = sorted_secs[-1] - sorted_secs[0]
    total_conn = max(aggregated[s]["total_conn"] for s in sorted_secs)
    total_disconn = max(aggregated[s]["total_disconn"] for s in sorted_secs)
    total_send = sum(aggregated[s]["send_pkt"] for s in sorted_secs)
    total_recv = sum(aggregated[s]["recv_pkt"] for s in sorted_secs)
    total_send_kb = sum(aggregated[s]["send_kb"] for s in sorted_secs)
    total_recv_kb = sum(aggregated[s]["recv_kb"] for s in sorted_secs)
    total_conn_fail = sum(aggregated[s]["conn_fail"] for s in sorted_secs)

    # Latency 통계 (전 구간)
    all_avg = []
    all_p99 = []
    global_min = float("inf")
    global_max = 0.0
    total_samples = 0

    for sec in sorted_secs:
        d = aggregated[sec]
        if d["lat_samples"] > 0:
            avg = d["lat_avg_sum"] / d["lat_samples"]
            all_avg.append(avg)
            all_p99.append(d["lat_p99_max"])
            if d["lat_min"] < global_min:
                global_min = d["lat_min"]
            if d["lat_max"] > global_max:
                global_max = d["lat_max"]
            total_samples += d["lat_samples"]

    print("\n" + "=" * 70)
    print("  LOAD TEST SUMMARY REPORT")
    print("=" * 70)

    hours = duration // 3600
    mins = (duration % 3600) // 60
    secs = duration % 60
    print(f"\n  Duration        : {hours}h {mins}m {secs}s ({duration}s)")
    print(f"  Peak Connections: {total_conn}")
    print(f"  Total Disconn   : {total_disconn}")
    print(f"  Connect Failures: {total_conn_fail}")

    print(f"\n  --- Throughput ---")
    print(f"  Total Send      : {total_send:,} pkts ({total_send_kb / 1024:.1f} MB)")
    print(f"  Total Recv      : {total_recv:,} pkts ({total_recv_kb / 1024:.1f} MB)")
    if duration > 0:
        print(f"  Avg Send/s      : {total_send // duration:,} pkts/s")
        print(f"  Avg Recv/s      : {total_recv // duration:,} pkts/s")

    print(f"\n  --- Latency (ms) ---")
    if all_avg:
        overall_avg = sum(all_avg) / len(all_avg)
        overall_p99 = max(all_p99)
        print(f"  Min             : {global_min:.2f}")
        print(f"  Avg             : {overall_avg:.2f}")
        print(f"  Max             : {global_max:.2f}")
        print(f"  P99 (worst)     : {overall_p99:.2f}")
        print(f"  Total Samples   : {total_samples:,}")
    else:
        print(f"  (no latency data)")

    # 안정성 판정
    print(f"\n  --- Stability ---")
    anomalies = detect_anomalies(aggregated)
    if not anomalies:
        print(f"  Result: PASS - No anomalies detected")
    else:
        print(f"  Result: WARNING - {len(anomalies)} anomaly intervals detected")
        print(f"\n  Anomaly Details (first 20):")
        for sec, reasons in anomalies[:20]:
            reason_str = ", ".join(reasons)
            print(f"    [{sec}s] {reason_str}")
        if len(anomalies) > 20:
            print(f"    ... and {len(anomalies) - 20} more")

    print("\n" + "=" * 70)


def generate_charts(aggregated, output_dir):
    """그래프 PNG 생성"""
    if not HAS_MATPLOTLIB:
        print("\n[!] matplotlib not installed. Skipping chart generation.")
        print("    Install: pip install matplotlib")
        return

    sorted_secs = sorted(aggregated.keys())
    if not sorted_secs:
        return

    times = sorted_secs
    total_conn = [aggregated[s]["total_conn"] for s in times]
    total_disconn = [aggregated[s]["total_disconn"] for s in times]
    send_pkt = [aggregated[s]["send_pkt"] for s in times]
    recv_pkt = [aggregated[s]["recv_pkt"] for s in times]
    lat_avg = []
    lat_p99 = []
    lat_max_list = []
    for s in times:
        d = aggregated[s]
        if d["lat_samples"] > 0:
            lat_avg.append(d["lat_avg_sum"] / d["lat_samples"])
            lat_p99.append(d["lat_p99_max"])
            lat_max_list.append(d["lat_max"])
        else:
            lat_avg.append(0)
            lat_p99.append(0)
            lat_max_list.append(0)

    disconn_per_sec = [aggregated[s]["disconn"] for s in times]

    # 시간 축 (분 단위)
    time_min = [s / 60.0 for s in times]

    fig, axes = plt.subplots(2, 2, figsize=(16, 10))
    fig.suptitle("Load Test Metrics", fontsize=14, fontweight="bold")

    # 1. Connections
    ax = axes[0][0]
    ax.plot(time_min, total_conn, "b-", label="Connected", linewidth=1.5)
    ax.plot(time_min, total_disconn, "r-", label="Total Disconn", linewidth=1.5)
    ax.set_title("Connections")
    ax.set_xlabel("Time (min)")
    ax.set_ylabel("Count")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 2. Throughput
    ax = axes[0][1]
    ax.plot(time_min, send_pkt, "g-", label="Send pkt/s", linewidth=1, alpha=0.8)
    ax.plot(time_min, recv_pkt, "b-", label="Recv pkt/s", linewidth=1, alpha=0.8)
    ax.set_title("Throughput (pkt/s)")
    ax.set_xlabel("Time (min)")
    ax.set_ylabel("Packets/s")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 3. Latency
    ax = axes[1][0]
    ax.plot(time_min, lat_avg, "b-", label="Avg", linewidth=1.5)
    ax.plot(time_min, lat_p99, "orange", label="P99", linewidth=1.5)
    ax.plot(time_min, lat_max_list, "r-", label="Max", linewidth=0.8, alpha=0.5)
    ax.set_title("Latency (ms)")
    ax.set_xlabel("Time (min)")
    ax.set_ylabel("ms")
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 4. Disconnect Rate
    ax = axes[1][1]
    ax.bar(time_min, disconn_per_sec, width=1.0 / 60, color="red", alpha=0.7)
    ax.set_title("Disconnections per Second")
    ax.set_xlabel("Time (min)")
    ax.set_ylabel("Count")
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    chart_path = os.path.join(output_dir, "load_test_report.png")
    plt.savefig(chart_path, dpi=150)
    plt.close()
    print(f"\nChart saved: {chart_path}")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    # CSV 파일 수집
    csv_files = []
    for arg in sys.argv[1:]:
        if os.path.isdir(arg):
            csv_files.extend(glob.glob(os.path.join(arg, "*.csv")))
        elif os.path.isfile(arg) and arg.endswith(".csv"):
            csv_files.append(arg)
        else:
            print(f"[!] Skipping: {arg}")

    if not csv_files:
        print("No CSV files found.")
        sys.exit(1)

    print(f"Files: {', '.join(os.path.basename(f) for f in csv_files)}")

    aggregated = load_csv_files(csv_files)
    print_summary(aggregated)

    output_dir = os.path.dirname(csv_files[0]) or "."
    generate_charts(aggregated, output_dir)


if __name__ == "__main__":
    main()
