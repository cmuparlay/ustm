import pandas as pd
import matplotlib.pyplot as plt

# Load the dataset
file_path = "data.csv"  # Update this with your actual file path
df = pd.read_csv(file_path)

# Identify variable parameters (exclude constants)
constant_columns = [col for col in df.columns if df[col].nunique() == 1]
variable_columns = [col for col in df.columns if col not in constant_columns + ["Command", "Percent Aborts", "Throughout (MOps)"]]

# Generate line plots for each variable parameter
for var_param in variable_columns:
    plt.figure(figsize=(10, 6))

    for stm_system in df["Command"].unique():
        subset = df[df["Command"] == stm_system]
        subset = subset.groupby(var_param, as_index=False).mean(numeric_only=True)

        plt.plot(subset[var_param], subset["Throughout (MOps)"], marker='o', label=stm_system.split('/')[-1])

    plt.xlabel(var_param)
    plt.ylabel("Throughput (MOps)")
    plt.title(f"Throughput vs {var_param}")
    plt.legend(title="STM System", bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.grid(True)
    plt.show()

    plt.figure(figsize=(10, 6))

    for stm_system in df["Command"].unique():
        subset = df[df["Command"] == stm_system]
        subset = subset.groupby(var_param, as_index=False).mean(numeric_only=True)

        plt.plot(subset[var_param], subset["Percent Aborts"], marker='o', label=stm_system.split('/')[-1])

    plt.xlabel(var_param)
    plt.ylabel("Percent Aborts")
    plt.title(f"Percent Aborts vs {var_param}")
    plt.legend(title="STM System", bbox_to_anchor=(1.05, 1), loc='upper left')
    plt.grid(True)
    plt.show()
