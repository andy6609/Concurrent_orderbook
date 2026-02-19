import csv
import matplotlib.pyplot as plt
import os
from collections import defaultdict

# ============================================================
# CSV 읽기
# ============================================================
def read_csv(filepath):
    results = []
    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            results.append({
                'workload':            row['workload'],
                'policy':              row['policy'],
                'threads':             int(row['threads']),
                'throughput_ops_per_sec': int(row['throughput_ops_per_sec']),
                'avg_latency_ns':      int(row['avg_latency_ns']),
                'p99_latency_ns':      int(row['p99_latency_ns']),
            })
    return results

# ============================================================
# 데이터 그룹핑
# ============================================================
def group_data(results, metric):
    """workload → policy → (threads_list, metric_list)"""
    grouped = defaultdict(lambda: defaultdict(lambda: ([], [])))
    for r in results:
        wl = r['workload']
        pl = r['policy']
        grouped[wl][pl][0].append(r['threads'])
        grouped[wl][pl][1].append(r[metric])
    return grouped

# ============================================================
# 그래프 1: Throughput vs Threads
# ============================================================
def plot_throughput(results, output_dir):
    grouped = group_data(results, 'throughput_ops_per_sec')
    workloads = ['read_heavy', 'balanced', 'write_heavy']
    workloads = [w for w in workloads if w in grouped]

    fig, axes = plt.subplots(1, len(workloads), figsize=(5 * len(workloads), 4), sharey=False)
    if len(workloads) == 1:
        axes = [axes]

    style = {
        'MutexPolicy':       {'color': '#e74c3c', 'marker': 's', 'label': 'mutex'},
        'SharedMutexPolicy': {'color': '#3498db', 'marker': 'o', 'label': 'shared_mutex'},
    }

    for ax, wl in zip(axes, workloads):
        for policy in ['MutexPolicy', 'SharedMutexPolicy']:
            if policy not in grouped[wl]:
                continue
            threads, throughput = grouped[wl][policy]
            pairs = sorted(zip(threads, throughput))
            xs, ys = zip(*pairs)
            ys_m = [v / 1_000_000 for v in ys]
            s = style[policy]
            ax.plot(xs, ys_m,
                    marker=s['marker'], color=s['color'], label=s['label'],
                    linewidth=2, markersize=6)

        ax.set_title(wl.replace('_', ' ').title(), fontsize=12, fontweight='bold')
        ax.set_xlabel('Threads')
        ax.set_xticks([1, 2, 4, 8])
        ax.grid(True, alpha=0.3)
        ax.legend()

    axes[0].set_ylabel('Throughput (M ops/sec)')
    fig.suptitle('Throughput: mutex vs shared_mutex', fontsize=14, fontweight='bold')
    plt.tight_layout()

    output_path = os.path.join(output_dir, 'throughput_comparison.png')
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")

# ============================================================
# 그래프 2: p99 Latency vs Threads
# ============================================================
def plot_p99_latency(results, output_dir):
    grouped = group_data(results, 'p99_latency_ns')
    workloads = ['read_heavy', 'balanced', 'write_heavy']
    workloads = [w for w in workloads if w in grouped]

    fig, axes = plt.subplots(1, len(workloads), figsize=(5 * len(workloads), 4), sharey=False)
    if len(workloads) == 1:
        axes = [axes]

    style = {
        'MutexPolicy':       {'color': '#e74c3c', 'marker': 's', 'label': 'mutex'},
        'SharedMutexPolicy': {'color': '#3498db', 'marker': 'o', 'label': 'shared_mutex'},
    }

    for ax, wl in zip(axes, workloads):
        for policy in ['MutexPolicy', 'SharedMutexPolicy']:
            if policy not in grouped[wl]:
                continue
            threads, p99 = grouped[wl][policy]
            pairs = sorted(zip(threads, p99))
            xs, ys = zip(*pairs)
            ys_us = [v / 1000 for v in ys]
            s = style[policy]
            ax.plot(xs, ys_us,
                    marker=s['marker'], color=s['color'], label=s['label'],
                    linewidth=2, markersize=6)

        ax.set_title(wl.replace('_', ' ').title(), fontsize=12, fontweight='bold')
        ax.set_xlabel('Threads')
        ax.set_xticks([1, 2, 4, 8])
        ax.grid(True, alpha=0.3)
        ax.legend()

    axes[0].set_ylabel('p99 Latency (μs)')
    fig.suptitle('p99 Latency: mutex vs shared_mutex', fontsize=14, fontweight='bold')
    plt.tight_layout()

    output_path = os.path.join(output_dir, 'p99_latency_comparison.png')
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f"Saved: {output_path}")

# ============================================================
# Main
# ============================================================
if __name__ == '__main__':
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    csv_path    = os.path.join(project_dir, 'results', 'benchmark_results.csv')
    output_dir  = os.path.join(project_dir, 'results')

    if not os.path.exists(csv_path):
        print(f"Error: {csv_path} not found. Run bench_comparison first.")
        exit(1)

    results = read_csv(csv_path)
    plot_throughput(results, output_dir)
    plot_p99_latency(results, output_dir)

    print("\nDone. Graphs saved in results/")
