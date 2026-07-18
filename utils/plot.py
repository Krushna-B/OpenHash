from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd

ROOT = Path(__file__).resolve().parent.parent
FIGDIR = ROOT / "figures"

# color follows the entity, in every figure (dataviz palette, fixed order)
COLORS = {
    "KVStore": "#2a78d6",  # blue
    "std::unordered_map": "#898781",  # muted gray: the baseline recedes
    "KVStoreOpen": "#1baf7a",  # aqua
    "KVStoreArena": "#4a3aa7",  # violet
    "LockedStore": "#e34948",  # red
    "ShardedStore": "#008300",  # green
}
LABELS = {
    "KVStore": "chaining",
    "std::unordered_map": "std::unordered_map",
    "KVStoreOpen": "open addressing",
    "KVStoreArena": "open + arena",
    "LockedStore": "global lock",
    "ShardedStore": "16 shards",
}
STORE_ORDER = ["KVStore", "std::unordered_map", "KVStoreOpen", "KVStoreArena"]

INK = "#0b0b0b"
MUTED = "#898781"
GRID = "#e1e0d9"
BASELINE = "#c3c2b7"

plt.rcParams.update(
    {
        "font.family": ["Helvetica Neue", "Arial", "DejaVu Sans"],
        "font.size": 10,
        "axes.labelsize": 10.5,
        "axes.titlesize": 11,
        "axes.edgecolor": BASELINE,
        "axes.labelcolor": INK,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
        "legend.frameon": False,
        "legend.fontsize": 9,
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "savefig.bbox": "tight",
    }
)


def style_axis(ax):
    ax.grid(axis="y", color=GRID, linewidth=0.7)
    ax.set_axisbelow(True)
    ax.tick_params(length=0)


def save(fig, name):
    FIGDIR.mkdir(exist_ok=True)
    for ext in ("png", "pdf"):
        fig.savefig(FIGDIR / f"{name}.{ext}", dpi=300)
    plt.close(fig)
    print(f"  wrote figures/{name}.png / .pdf")


def fig_single_thread(df):
    """Grouped bars: insert/get medians per store, min-max whiskers."""
    for (klen, vlen), cfg in df.groupby(["key_len", "value_len"]):
        stores = [s for s in STORE_ORDER if s in cfg["store"].unique()]
        agg = cfg.groupby("store")[["insert_mops", "get_mops"]].agg(
            ["median", "min", "max"]
        )

        fig, axes = plt.subplots(1, 2, figsize=(7.2, 3.0), sharey=True)
        for ax, metric, title in zip(
            axes, ["insert_mops", "get_mops"], ["insert", "get"]
        ):
            for i, s in enumerate(stores):
                med = agg.loc[s, (metric, "median")]
                lo = med - agg.loc[s, (metric, "min")]
                hi = agg.loc[s, (metric, "max")] - med
                ax.bar(i, med, width=0.62, color=COLORS[s], zorder=3)
                ax.errorbar(
                    i,
                    med,
                    yerr=[[lo], [hi]],
                    fmt="none",
                    ecolor=INK,
                    elinewidth=1,
                    capsize=3,
                    zorder=4,
                )
                ax.text(
                    i,
                    med + hi + 0.15,
                    f"{med:.1f}",
                    ha="center",
                    va="bottom",
                    fontsize=9,
                    color=INK,
                )
            ax.set_xticks(range(len(stores)))
            ax.set_xticklabels([LABELS[s] for s in stores], rotation=20, ha="right")
            ax.set_title(title, loc="left", color=INK)
            style_axis(ax)
        axes[0].set_ylabel("throughput (Mops/s)")
        fig.suptitle(
            f"1M keys ({klen}B), values {vlen}B — median of trials, whiskers = min–max",
            x=0.01,
            ha="left",
            fontsize=9,
            color=MUTED,
            y=1.04,
        )
        save(fig, f"single_thread_k{klen}_v{vlen}")


def fig_working_set(df):
    """Get throughput vs key count (log x), one figure per value size."""
    for vlen, sweep in df.groupby("value_len"):
        _fig_working_set_one(sweep, vlen)


def _fig_working_set_one(sweep, vlen):
    if sweep["num_keys"].nunique() < 3:
        print(f"  (skipping working-set figure v{vlen}: need a num_keys sweep)")
        return

    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    for s in STORE_ORDER:
        d = sweep[sweep["store"] == s]
        if d.empty:
            continue
        g = d.groupby("num_keys")["get_mops"].agg(["median", "min", "max"])
        ax.plot(
            g.index,
            g["median"],
            color=COLORS[s],
            linewidth=2,
            marker="o",
            markersize=4,
            label=LABELS[s],
            zorder=3,
        )
        ax.fill_between(
            g.index,
            g["min"],
            g["max"],
            color=COLORS[s],
            alpha=0.15,
            linewidth=0,
            zorder=2,
        )

    ax.set_xscale("log")
    ax.set_xlabel("distinct keys (working set grows left to right)")
    ax.set_ylabel("get throughput (Mops/s)")
    ax.legend(loc="lower left")
    style_axis(ax)

    # approximate cache boundaries for the open-addressing slot array
    # (~112 B/key at load factor 0.5): L2 ~16 MB, DRAM beyond system cache
    for n, label in [(16e6 / 112, "≈ L2 capacity"), (2e6, "DRAM-resident")]:
        ax.axvline(n, color=BASELINE, linewidth=0.8, linestyle=(0, (4, 3)))
        ax.text(
            n, ax.get_ylim()[1] * 0.97, f" {label}", fontsize=8, color=MUTED, va="top"
        )

    save(fig, f"working_set_staircase_v{vlen}")


def fig_matrix(df, metric):
    """Small multiples: rows = value size, cols = key count, bars = stores."""
    from matplotlib.patches import Patch

    keys_list = sorted(df["num_keys"].unique())
    vlens = sorted(df["value_len"].unique())
    if len(keys_list) < 2 or len(vlens) < 2:
        print("  (skipping matrix figure: need multiple key counts AND value sizes)")
        return

    fig, axes = plt.subplots(
        len(vlens),
        len(keys_list),
        figsize=(2.3 * len(keys_list) + 0.8, 1.9 * len(vlens) + 0.9),
        sharey="col",
        squeeze=False,
    )

    for r, vlen in enumerate(vlens):
        for c, nk in enumerate(keys_list):
            ax = axes[r][c]
            cfg = df[(df["value_len"] == vlen) & (df["num_keys"] == nk)]
            stores = [s for s in STORE_ORDER if s in cfg["store"].unique()]
            for i, s in enumerate(stores):
                g = cfg[cfg["store"] == s][metric]
                med = g.median()
                ax.bar(i, med, width=0.7, color=COLORS[s], zorder=3)
                ax.errorbar(
                    i,
                    med,
                    yerr=[[med - g.min()], [g.max() - med]],
                    fmt="none",
                    ecolor=INK,
                    elinewidth=0.8,
                    capsize=2,
                    zorder=4,
                )
            ax.set_xticks([])
            style_axis(ax)
            if r == 0:
                ax.set_title(f"{nk:,} keys", fontsize=9.5, color=INK)
            if c == 0:
                ax.set_ylabel(f"{vlen}B values\nMops/s", fontsize=9)

    handles = [Patch(color=COLORS[s], label=LABELS[s]) for s in STORE_ORDER]
    fig.legend(handles=handles, ncol=4, loc="lower center", bbox_to_anchor=(0.5, -0.04))
    op = metric.split("_")[0]
    fig.suptitle(
        f"{op} throughput across workloads (median, whiskers = min–max)",
        x=0.01,
        ha="left",
        fontsize=9.5,
        color=MUTED,
    )
    fig.tight_layout(rect=(0, 0.03, 1, 0.97))
    save(fig, f"matrix_{op}")


def fig_thread_scaling(path, baselines={}):
    """Threads vs throughput: global lock vs sharded, read-only vs mixed."""
    if not path.exists():
        print(
            "  (skipping thread-scaling figure: no results_threaded.csv — "
            "run ./build/Benchmark 64 1000000 threads)"
        )
        return
    tdf = pd.read_csv(path)
    for vlen, df in tdf.groupby("value_len"):
        _fig_thread_scaling_one(df, vlen, baselines.get(vlen))


def _fig_thread_scaling_one(df, vlen, baseline):
    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    if baseline:
        ax.axhline(baseline, color=MUTED, linewidth=1, linestyle=(0, (2, 2)), zorder=1)
        ax.text(
            1,
            baseline * 1.04,
            "fastest single-threaded store (no lock)",
            fontsize=8,
            color=MUTED,
            va="bottom",
        )
    for store in ["LockedStore", "ShardedStore"]:
        for we, dash, suffix in [
            (0, "-", "read-only"),
            (10, (0, (4, 2)), "10% writes"),
        ]:
            d = df[(df["store"] == store) & (df["write_every"] == we)]
            if d.empty:
                continue
            g = d.groupby("threads")["mops"].median()
            ax.plot(
                g.index,
                g.values,
                color=COLORS[store],
                linestyle=dash,
                linewidth=2,
                marker="o",
                markersize=4.5,
                zorder=3,
            )
            ax.text(
                g.index[-1] * 1.03,
                g.values[-1],
                f"{LABELS[store]}, {suffix}",
                fontsize=8.5,
                color=COLORS[store],
                va="center",
            )

    ax.set_xscale("log", base=2)
    ax.set_xticks([1, 2, 4, 8])
    ax.set_xticklabels(["1", "2", "4", "8"])
    ax.set_xlim(right=ax.get_xlim()[1] * 2.6)  # room for end labels
    ax.set_xlabel("threads")
    ax.set_ylabel("throughput (Mops/s)")
    style_axis(ax)
    save(fig, f"thread_scaling_v{vlen}")


def fig_concurrent_bars(df, tpath):
    """Single-threaded stores vs concurrent wrappers at their best thread
    count, one bar figure per value size. Hatched = 10% writes."""
    if not tpath.exists():
        print("  (skipping concurrent-bars figure: no results_threaded.csv)")
        return
    tdf = pd.read_csv(tpath)

    for vlen in sorted(set(tdf["value_len"]) & set(df["value_len"])):
        single = df[(df["num_keys"] == 1_000_000) & (df["value_len"] == vlen)]
        th = tdf[tdf["value_len"] == vlen]
        if single.empty or th.empty:
            continue

        # (label, mops, color, hatch, annotation)
        bars = []
        for s in STORE_ORDER:
            g = single[single["store"] == s]["get_mops"]
            if not g.empty:
                bars.append((LABELS[s], g.median(), COLORS[s], None, "1 thread"))
        for st in ["LockedStore", "ShardedStore"]:
            for we, suffix, hatch in [(0, "read-only", None),
                                      (10, "10% writes", "//")]:
                d = th[(th["store"] == st) & (th["write_every"] == we)]
                if d.empty:
                    continue
                g = d.groupby("threads")["mops"].median()
                t = g.idxmax()
                bars.append((f"{LABELS[st]}\n{suffix}", g.max(), COLORS[st],
                             hatch, f"{t} thread{'s' if t > 1 else ''}"))

        fig, ax = plt.subplots(figsize=(8.8, 3.4))
        for i, (label, mops, color, hatch, note) in enumerate(bars):
            ax.bar(i, mops, width=0.66, color=color, hatch=hatch,
                   edgecolor="white" if hatch else color, linewidth=0.5,
                   zorder=3)
            ax.text(i, mops + 0.15, f"{mops:.1f}", ha="center", va="bottom",
                    fontsize=9, color=INK)
        ax.set_xticks(range(len(bars)))
        ax.set_xticklabels(
            [f"{b[0].replace('std::', '')}\n{b[4]}" for b in bars],
            fontsize=8)
        ax.set_ylabel("get throughput (Mops/s)")
        fig.suptitle(
            f"1M keys, {vlen}B values — single-threaded stores vs concurrent "
            "wrappers at their best thread count", x=0.01, ha="left",
            fontsize=9, color=MUTED)
        style_axis(ax)
        save(fig, f"concurrent_vs_single_v{vlen}")


def fig_hierarchy(path):
    """ns per get vs working-set bytes: latency (chase) vs throughput cost."""
    if not path.exists():
        print(
            "  (skipping hierarchy figure: no results_hierarchy.csv — "
            "run ./build/Hierarchy)"
        )
        return
    df = pd.read_csv(path)

    fig, ax = plt.subplots(figsize=(6.4, 3.6))
    series = [
        ("chase_ns_per_op", "#2a78d6", "dependent chain (true latency)"),
        ("tput_ns_per_op", "#1baf7a", "independent gets (misses overlap)"),
    ]
    for col, color, label in series:
        g = df.groupby("bytes")[col].agg(["median", "min", "max"])
        ax.plot(
            g.index,
            g["median"],
            color=color,
            linewidth=2,
            marker="o",
            markersize=4,
            label=label,
            zorder=3,
        )
        ax.fill_between(
            g.index, g["min"], g["max"], color=color, alpha=0.15, linewidth=0, zorder=2
        )

    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_yticks([25, 50, 100, 200, 400])
    ax.set_yticklabels(["25", "50", "100", "200", "400"])
    ax.minorticks_off()
    ax.set_xticks([32 << 10, 128 << 10, 1 << 20, 16 << 20, 512 << 20])
    ax.set_xticklabels(["32 KiB", "128 KiB", "1 MiB", "16 MiB", "512 MiB"])
    ax.set_xlabel("working-set size")
    ax.set_ylabel("ns per get (log)")
    ax.legend(loc="upper left")
    style_axis(ax)

    for x, label in [(128 << 10, "L1d"), (16 << 20, "L2")]:
        ax.axvline(x, color=BASELINE, linewidth=0.8, linestyle=(0, (4, 3)))
        ax.text(
            x, ax.get_ylim()[1] * 0.92, f" {label}", fontsize=8, color=MUTED, va="top"
        )

    save(fig, "memory_hierarchy")


def main():
    results = ROOT / "results.csv"
    baselines = {}
    if results.exists():
        df = pd.read_csv(results)
        print(f"loaded {len(df)} rows from results.csv")
        fig_single_thread(df[df["num_keys"] == 1_000_000])
        fig_working_set(df)
        fig_matrix(df, "get_mops")
        fig_matrix(df, "insert_mops")
        big = df[df["num_keys"] == 1_000_000]
        baselines = {
            v: g.groupby("store")["get_mops"].median().max()
            for v, g in big.groupby("value_len")
        }
    else:
        print("  (skipping single-thread + working-set figures: no results.csv)")
    fig_thread_scaling(ROOT / "results_threaded.csv", baselines)
    if results.exists():
        fig_concurrent_bars(df, ROOT / "results_threaded.csv")
    fig_hierarchy(ROOT / "results_hierarchy.csv")


if __name__ == "__main__":
    main()
