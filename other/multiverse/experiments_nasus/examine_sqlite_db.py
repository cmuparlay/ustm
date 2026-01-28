import sqlite3
import os


def read_column_from_db(db_relative_path: str, table: str):
    db_path = os.path.abspath(db_relative_path)

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    try:
        query = f"SELECT alg, throughput_per_joules, TOTAL_THREADS FROM {table} WHERE UPDATE_ONLY_THREADS=16 and RQ=0.01 group by alg, TOTAL_THREADS;"
        cursor.execute(query)

        results = cursor.fetchall()
        return results

    finally:
        conn.close()


if __name__ == "__main__":
    values = read_column_from_db("data_abtree_node0_power_pkg/output_database.sqlite", "data")
    for v in values:
        print(v)
