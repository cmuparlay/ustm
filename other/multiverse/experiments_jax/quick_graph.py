import pandas as pd
import matplotlib.pyplot as plt

# Load the CSV
df = pd.read_csv("throughput.csv")

# Plot throughput
plt.figure(figsize=(8, 5))
plt.plot(df["time_sec"], df["throughput_ops_sec"], marker="o", linestyle="-")

# Axis labels
plt.ylabel("Throughput (ops/sec)")

# X-axis: tick marks only (no labels)
plt.xticks(df["time_sec"], labels=[""] * len(df["time_sec"]))

# Grid for readability
plt.grid(True, linestyle="--", alpha=0.6)

plt.title("Throughput Over Time")
plt.tight_layout()

# Save to file
plt.savefig("throughput_over_time.png", dpi=150)
