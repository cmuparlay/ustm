import os
import glob
import matplotlib.pyplot as plt
import numpy as np

color_list = [
      '#df7126' # 0
    , '#37946e' # 1
    , '#FFFFFF' # 2
    , '#76428a' # 3
    , '#ac3232' # 4
    , '#fbf236' # 5
    , '#b0aca0' # 6
    , '#9e0b00' # 7
    , '#000000' # 8
    , '#fbf236' # 9
    , '#2d4bad' # 10
    , '#196583' # 11
    , '#FFFFFF' #12
    , '#076794' #13
    , '#9db2bd' #14
]

host = "nasus"

# ---- Configurable colors per algorithm ----
algo_colors = {
    "brown_abtree_tm_auto.norec_" + str(host): color_list[1],
    "brown_abtree_tm_auto.tl2_" + str(host): color_list[14],
    "abtree.dcotl_" + str(host): color_list[4],
    "abtree.multiverse_" + str(host): color_list[5],
    "abtree.tinystm": color_list[6],
    "abtree.multiverse_mode1_only_" + str(host): color_list[0],
    "abtree.multiverse_mode2_only_" + str(host): color_list[8],
}

algo_alias = {
    "brown_abtree_tm_auto.norec_" + str(host): "NOrec",
    "abtree.tinystm_" + str(host): "TinySTM",
    "brown_abtree_tm_auto.tl2_" + str(host): "TL2",
    "abtree.dcotl_" + str(host): "DCTL",
    "abtree.multiverse_" + str(host): "Multiverse",
    "abtree.multiverse_mode1_only_" + str(host): "Multiverse-ModeQ-Only",
    "abtree.multiverse_mode2_only_" + str(host): "Multiverse-ModeU-Only",
}

data_folder = "data/"


# ---- Parse all files ----
data = {}  # {algorithm: {time: [throughputs]}}
pattern = os.path.join(data_folder, "data*.txt")

for fname in glob.glob(pattern):
    algo = None
    throughputs = []  # collect (time, thr) pairs first
    with open(fname, "r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("Throughput_per_sec="):
                _, rest = line.split("=", 1)
                time_str, thr_str = rest.split(",")
                t = float(time_str)
                thr = float(thr_str)
                throughputs.append((t, thr))
            elif line.startswith("algorithm="):
                algo = line.split("=", 1)[1]

    if algo is None:
        raise ValueError(f"No algorithm line found in {fname}")

    if algo not in data:
        data[algo] = {}
    for t, thr in throughputs:
        data[algo].setdefault(t, []).append(thr)

# ---- Print algorithms found ----
print("Algorithms found:")
for algo in data.keys():
    print(f"  - {algo}")

# ---- Compute averages ----
avg_data = {}  # {algorithm: (times[], avg_throughputs[])}
for algo, series in data.items():
    times = sorted(series.keys())
    avg_thr = [np.mean(series[t]) for t in times]
    avg_data[algo] = (times, avg_thr)

# ---- Plot main figure ----
fig, ax = plt.subplots(figsize=(8, 5))

for algo, (times, avg_thr) in avg_data.items():
    color = algo_colors.get(algo, None)
    label = algo_alias.get(algo, algo)  # use alias if available
    ax.plot(times, avg_thr, label=label, color=color, marker="o", linestyle="-")

# ax.set_yscale("log")
ax.set_xlabel("Time (s)")
ax.set_ylabel("Average Throughput (ops/sec)")
# ax.set_title("Average Throughput Over Time (5 trials per algorithm)")

# Keep x-axis ticks only on bottom, no labels
xticks = sorted(list({t for _, (times, _) in avg_data.items() for t in times}))

ax.set_xticks([])
ax.tick_params(axis='x', which='both', bottom=False, top=False)

ax.grid(True, linestyle="--", alpha=0.6)
fig.tight_layout()

# Save main plot without legend
fig.savefig("average_throughput.png", dpi=150, bbox_inches="tight")

# ---- Save legend separately ----
handles, labels = ax.get_legend_handles_labels()

# Reorder according to algo_alias keys
ordered_handles = []
ordered_labels = []
for key in algo_alias.keys():
    if key in avg_data:  # only include if this algo was found
        # find its label in the collected labels
        alias_label = algo_alias[key]
        for h, l in zip(handles, labels):
            if l == alias_label:
                ordered_handles.append(h)
                ordered_labels.append(l)

legend_fig = plt.figure(figsize=(6, 1))
legend_ax = legend_fig.add_subplot(111)
legend_ax.axis("off")
legend = legend_ax.legend(
    ordered_handles, ordered_labels,
    loc="center",
    # ncol=len(ordered_labels),
    ncol=4,
    frameon=False
)
legend_fig.savefig("average_throughput_legend.png", dpi=150, bbox_inches="tight")