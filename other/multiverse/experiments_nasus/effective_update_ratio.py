import os
# import matplotlib.pyplot as plt
from collections import defaultdict
import statistics

# ---------- Global directory ----------
DATA_DIR = "data_abtree_node0_memory_usage"

# ---------- Fields we care about ----------
DESIRED_FIELDS = {
    "algorithm",
    "TOTAL_THREADS",
    "INS_DEL_FRAC",
    "RQ",
    "UPDATE_ONLY_THREADS",
    "total_dedicated_update_ops",
    "total_updates",
    "total_find",
}

# ---------- Parse a single file ----------
def parse_stats_file(path):
    data = {}
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            value = value.strip()
            if key not in DESIRED_FIELDS:
                continue

            if key == "INS_DEL_FRAC":
                parts = value.split()
                data[key] = tuple(float(p) for p in parts)
            elif key in {"TOTAL_THREADS", "total_dedicated_update_ops", "total_updates", "total_find", "UPDATE_ONLY_THREADS"}:
                data[key] = int(value)
            elif key == "RQ":
                data[key] = float(value)
            else:
                data[key] = value

    if not all(k in data for k in DESIRED_FIELDS):
        return None
    return data

# ---------- Load all files and compute metric ----------
def load_all_data():
    results = defaultdict(list)
    for fname in os.listdir(DATA_DIR):
        if not fname.startswith("data"):
            continue
        path = os.path.join(DATA_DIR, fname)
        parsed = parse_stats_file(path)
        if parsed is None:
            continue

        total_up = parsed["total_dedicated_update_ops"] + parsed["total_updates"]
        denom = total_up + parsed["total_find"]
        if denom == 0:
            continue
        value = total_up / denom

        key = (
            parsed["algorithm"],
            parsed["INS_DEL_FRAC"],
            parsed["RQ"],
            parsed["UPDATE_ONLY_THREADS"],
            parsed["TOTAL_THREADS"]
        )
        results[key].append(value)

    # Average values for duplicate configurations
    averaged = {k: statistics.mean(v) for k, v in results.items()}
    return averaged

# ---------- Print results ----------
def print_results(averaged):
    grouped = defaultdict(list)
    for (alg, frac, rq, update_thr, th), val in averaged.items():
        grouped[(frac, rq, update_thr)].append((alg, th, val))

    for (frac, rq, update_thr), rows in grouped.items():
        print(f"\n=== INS_DEL_FRAC={frac}, RQ={rq}, UPDATE_ONLY_THREADS={update_thr} ===")
        for alg, th, val in sorted(rows, key=lambda x: (x[0], x[1])):
            print(f"algorithm={alg:10s} threads={th:3d}  value={val:.6f}")

# # ---------- Plot results ----------
# def plot_results(averaged):
#     grouped = defaultdict(list)
#     for (alg, frac, rq, update_thr, th), val in averaged.items():
#         grouped[(frac, rq, update_thr)].append((alg, th, val))

#     for (frac, rq, update_thr), rows in grouped.items():
#         algorithms = sorted({r[0] for r in rows})
#         threads = sorted({r[1] for r in rows})

#         data_map = {alg: {t: 0.0 for t in threads} for alg in algorithms}
#         for alg, th, val in rows:
#             data_map[alg][th] = val

#         plt.figure(figsize=(10, 6))
#         num_alg = len(algorithms)
#         width = 0.8 / num_alg
#         offsets = [(i - num_alg/2) * width for i in range(num_alg)]

#         for i, alg in enumerate(algorithms):
#             y_vals = [data_map[alg][t] for t in threads]
#             plt.bar([t + offsets[i] for t in threads], y_vals, width=width, label=alg)

#         plt.xticks(threads, [str(t) for t in threads])
#         plt.xlabel("TOTAL_THREADS")
#         plt.ylabel("update_ratio = (dedicated+updates)/total_ops")
#         plt.title(f"INS_DEL_FRAC={frac}, RQ={rq}, UPDATE_ONLY_THREADS={update_thr}")
#         plt.legend()
#         plt.tight_layout()
#         plt.show()

# ---------- Main ----------
if __name__ == "__main__":
    averaged = load_all_data()
    print_results(averaged)
    # plot_results(averaged)  # Uncomment to plot
