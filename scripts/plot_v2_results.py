import csv
import os
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

RESULTS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'results')

# ── helpers ───────────────────────────────────────────────────────────────────
def read_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))

def save(fig, name):
    path = os.path.join(RESULTS_DIR, name)
    fig.savefig(path, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"Saved: {path}")

# ── Plot 1: Pool vs Default allocator ─────────────────────────────────────────
def plot_pool(rows):
    labels, tput, avg_lat, p99_lat = [], [], [], []
    for r in rows:
        short = 'OrderPool' if 'OrderPool' in r['allocator'] else 'Default\nallocator'
        labels.append(short)
        tput.append(int(r['throughput_ops_per_sec']) / 1_000)
        avg_lat.append(int(r['avg_latency_ns']) / 1_000)
        p99_lat.append(int(r['p99_latency_ns']) / 1_000)

    colors = ['#e74c3c', '#95a5a6']
    x = range(len(labels))

    fig, axes = plt.subplots(1, 3, figsize=(13, 4))
    fig.suptitle('Memory Allocation: OrderPool vs Default Allocator\n'
                 '(500k add + cancel, single thread)', fontsize=13, fontweight='bold')

    for ax, vals, ylabel, title in zip(
        axes,
        [tput, avg_lat, p99_lat],
        ['Throughput (k ops/s)', 'Avg latency (μs)', 'p99 latency (μs)'],
        ['Throughput', 'Avg Latency', 'p99 Latency'],
    ):
        bars = ax.bar(x, vals, color=colors, width=0.5, edgecolor='white', linewidth=0.8)
        ax.set_xticks(list(x))
        ax.set_xticklabels(labels, fontsize=10)
        ax.set_ylabel(ylabel)
        ax.set_title(title, fontweight='bold')
        ax.grid(axis='y', alpha=0.3)
        ax.spines['top'].set_visible(False)
        ax.spines['right'].set_visible(False)
        for bar, val in zip(bars, vals):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() * 1.02,
                    f'{val:,.1f}', ha='center', va='bottom', fontsize=9)

    plt.tight_layout()
    save(fig, 'pool_comparison.png')

# ── Plot 2: Sharding throughput scaling ───────────────────────────────────────
def plot_sharding(rows):
    single, sharded = {}, {}
    for r in rows:
        tc   = int(r['threads'])
        tput = int(r['throughput_ops_per_sec']) / 1_000
        if r['mode'] == 'single':
            single[tc] = tput
        else:
            sharded[tc] = tput

    threads  = sorted(single.keys())
    s_tput   = [single[t]  for t in threads]
    sh_tput  = [sharded[t] for t in threads]

    fig, axes = plt.subplots(1, 2, figsize=(11, 4))
    fig.suptitle('Sharding: Single Book vs ShardedOrderBook (4 symbols)',
                 fontsize=13, fontweight='bold')

    # Left: throughput vs threads
    ax = axes[0]
    ax.plot(threads, s_tput,  marker='s', color='#95a5a6', linewidth=2,
            markersize=7, label='Single book (1 lock)')
    ax.plot(threads, sh_tput, marker='o', color='#e74c3c', linewidth=2,
            markersize=7, label='Sharded (4 locks)')
    ax.set_xlabel('Threads')
    ax.set_ylabel('Throughput (k ops/s)')
    ax.set_title('Throughput vs Thread Count', fontweight='bold')
    ax.set_xticks(threads)
    ax.legend()
    ax.grid(alpha=0.3)
    ax.spines['top'].set_visible(False)
    ax.spines['right'].set_visible(False)

    # Right: speedup ratio
    ax2 = axes[1]
    speedups = [sh / s for sh, s in zip(sh_tput, s_tput)]
    bars = ax2.bar(threads, speedups, color='#e74c3c', width=0.6,
                   edgecolor='white', linewidth=0.8)
    ax2.axhline(1.0, color='#95a5a6', linestyle='--', linewidth=1.2, label='baseline (1×)')
    ax2.set_xlabel('Threads')
    ax2.set_ylabel('Sharding speedup (×)')
    ax2.set_title('Sharding Gain vs Single Book', fontweight='bold')
    ax2.set_xticks(threads)
    ax2.legend()
    ax2.grid(axis='y', alpha=0.3)
    ax2.spines['top'].set_visible(False)
    ax2.spines['right'].set_visible(False)
    for bar, val in zip(bars, speedups):
        ax2.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.03,
                 f'{val:.2f}×', ha='center', va='bottom', fontsize=9)

    plt.tight_layout()
    save(fig, 'sharding_comparison.png')

# ── Main ──────────────────────────────────────────────────────────────────────
if __name__ == '__main__':
    pool_csv     = os.path.join(RESULTS_DIR, 'pool_results.csv')
    sharding_csv = os.path.join(RESULTS_DIR, 'sharding_results.csv')

    if not os.path.exists(pool_csv):
        print(f"Missing {pool_csv} — run bench_pool first")
        exit(1)
    if not os.path.exists(sharding_csv):
        print(f"Missing {sharding_csv} — run bench_sharding first")
        exit(1)

    plot_pool(read_csv(pool_csv))
    plot_sharding(read_csv(sharding_csv))
    print("\nDone.")
