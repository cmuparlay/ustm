import sqlite3
import sys
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict


def main(db_path):
    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    # Get all table names
    cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND name='data';")
    tables = [row[0] for row in cursor.fetchall()]

    for table in tables:
        print(f"\nTable: {table}")
        cursor.execute(f"PRAGMA table_info({table});")
        columns = [row[1] for row in cursor.fetchall()]
        print("Columns:", columns)

        if all(col in columns for col in ["alg", "total_ops", "joules", "WORK_THREADS"]):
            summary_table = f"{table}_summary"

            cursor.execute(f"SELECT INS_DEL_FRAC FROM data;")
            rows = cursor.fetchall()
            col_names = [desc[0] for desc in cursor.description]

            print("\nSummary Table Contents:")
            print("\t".join(col_names))
            for row in rows:
                print("\t".join(str(val) for val in row))

            print(f"Creating new table: {summary_table}")
            cursor.execute(f"DROP TABLE IF EXISTS {summary_table}")
            cursor.execute(f"""
                CREATE TABLE {summary_table} AS
                SELECT
                	alg,
                    WORK_THREADS,
                    INS_DEL_FRAC,
                    AVG(total_ops),
                    AVG(joules),
                    (300.0 * 20) / AVG(total_ops) AS tdp_per_op,
                    joules / AVG(total_ops) as joules_per_op
                FROM {table}
                GROUP BY alg, WORK_THREADS, INS_DEL_FRAC;
            """)
            conn.commit()
            print(f"Created table '{summary_table}'")

            ins_del_fracs = ['5.0 5.0', '25.0 25.0']

            for frac in ins_del_fracs:
                # Fetch and print data from summary table
                cursor.execute(f"SELECT alg, WORK_THREADS, joules_per_op FROM {summary_table} WHERE INS_DEL_FRAC='" + str(frac) + "' ORDER BY alg, INS_DEL_FRAC, WORK_THREADS;")
                rows = cursor.fetchall()
                col_names = [desc[0] for desc in cursor.description]

                print("\nSummary Table Contents:")
                print("\t".join(col_names))
                for row in rows:
                    print("\t".join(str(val) for val in row))

                # Fetch and print data from summary table
                cursor.execute(f"SELECT alg, WORK_THREADS, joules_per_op FROM {summary_table} WHERE INS_DEL_FRAC='" + str(frac) + "' ORDER BY alg, INS_DEL_FRAC, WORK_THREADS;")
                rows = cursor.fetchall()
                col_names = [desc[0] for desc in cursor.description]

                print("\nSummary Table Contents:")
                print("\t".join(col_names))
                for row in rows:
                    print("\t".join(str(val) for val in row))


                # Prepare data for plotting
                data = defaultdict(dict)  # {WORK_THREADS: {alg: joules_per_op}}
                algs = set()
                thread_counts = set()

                for alg, threads, joules_per_op, in rows:
                    data[threads][alg] = joules_per_op
                    algs.add(alg)
                    thread_counts.add(threads)

                # algs = sorted(algs)
                algs = ['NV-HALT-CL',
                    'NV-HALT-CL-NOOPF',                
                    'Trinity',
                    'Trinity-NOOPF',
                    'SPHT',
                    'SPHT-NOOPF'
                ]
                thread_counts = sorted(thread_counts)
                x = np.arange(len(thread_counts))  # the label locations
                width = 0.8 / len(algs)  # width of each bar

                plt.rcParams.update({'font.size': 48})

                fig, ax = plt.subplots(figsize=(18, 12))

                alg_colors = {
                    'NV-HALT-CL': '#2d4bad',
                    'NV-HALT-CL-NOOPF': '#2d4bad',
                    'SPHT': '#fbf236',
                    'SPHT-NOOPF': '#fbf236',
                    'Trinity': '#b0aca0',
                    'Trinity-NOOPF': '#b0aca0',
                }

                alg_hatches = {
                    'NV-HALT-CL': '',
                    'NV-HALT-CL-NOOPF': '/',
                    'SPHT': '',
                    'SPHT-NOOPF': '/',
                    'Trinity': '',
                    'Trinity-NOOPF': '/',                
                }

                for i, alg in enumerate(algs):
                    values = [data[threads].get(alg, 0) for threads in thread_counts]
                    color = alg_colors.get(alg, None)
                    hatch = alg_hatches.get(alg, None)
                    ax.bar(x + i * width, values, width, label=alg, color=color, hatch=hatch, edgecolor='black', linewidth=2.0)


                # ax.set_xlabel("WORK_THREADS")
                # ax.set_ylabel("Joules per Operation")
                # ax.set_title("Energy Efficiency by Algorithm and Thread Count")
                ax.set_xticks(x + width * (len(algs) - 1) / 2)
                ax.set_xticklabels(thread_counts)
                ax.yaxis.grid(True, linestyle='-', linewidth=1.0, color='gray', alpha=0.5)
                ax.set_axisbelow(True)  # Ensure grid is under the bars
                # ax.legend()

                plt.tight_layout()
                plt.savefig("my_plot_" + str(frac) + ".png")  # Save the plot to a file
                print("Plot saved to my_plot.png")

    conn.close()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py path_to_db.sqlite")
    else:
        main(sys.argv[1])
