#pragma once

#ifdef GSTATS_HANDLE_STATS
#   ifndef __AND
#      define __AND ,
#   endif
#   define GSTATS_HANDLE_STATS_USER(gstats_handle_stat) \
        gstats_handle_stat(LONG_LONG, prefill_item_size, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, prefill_order_size, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        \
        gstats_handle_stat(LONG_LONG, txn_customer_new_order_txn, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, txn_customer_lookup_order_history_over_date_range, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, txn_company_add_new_item, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, txn_company_update_new_item, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
	gstats_handle_stat(LONG_LONG, txn_single_item_lookup, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, txn_analytics_lookup_order_value_over_date_range, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        gstats_handle_stat(LONG_LONG, wrset_size_at_abort, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, wrset_size_at_commit, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, snapshot_wrset_size_at_abort, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, snapshot_wrset_size_at_commit, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, rdset_size_at_abort, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, rdset_size_at_commit, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        gstats_handle_stat(LONG_LONG, failed_add_itemId_index, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, failed_add_orderId_index, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, successful_add_key, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, new_row, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, found_itemId, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, successful_val_traversal, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, categoryDist, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, numUnversions, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        \
        gstats_handle_stat(LONG_LONG, commits, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, commits_snapshot, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, commits_mode2_snapshot, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        \
        gstats_handle_stat(LONG_LONG, snapshot_starts, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, mode2_snapshot_starts, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        \
        gstats_handle_stat(LONG_LONG, snapshot_num_writer_successful_tryVersions, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, snapshot_num_successful_tryVersions, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, snapshot_num_successful_tryVersions_per_txn, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
        }) \
        gstats_handle_stat(LONG_LONG, snapshot_num_failed_tryVersions_per_txn, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
        }) \
        gstats_handle_stat(LONG_LONG, snapshot_num_failed_tryVersions, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        \
        gstats_handle_stat(LONG_LONG, aborts, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_read_only_txns, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_snapshot, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_snapshot_tryVersion_read_inconsistent, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_writer_failed_rdset_validation, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_snapshot_vlist_failed_find_ts, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_mode2_snapshot_vlist_failed_find_ts, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_mode2_snapshot_addr_locked_and_unversioned, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_mode2_snapshot_addr_lock_changed_after_read, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_tl2_load_read_inconsistent, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_tl2_store_failed_lock_acquire, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_tl2_store_write_inconsistent, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_mode2_snapshot, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_before_commit, 100000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, mode2_found_inprog_versioning_retries, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, mode2_found_locked_retry, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, mode2_failed_version_retry, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        \
        gstats_handle_stat(LONG_LONG, snapshot_read_count_at_abort, 10000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, read_count_at_abort, 10000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, read_count_at_commit, 10000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, company_analytics_txn_point_queries, 1, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
        }) \
        gstats_handle_stat(LONG_LONG, mode2_try_versioning, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, num_mode2_forced_back_to_mode1, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, aborts_before_mode2, 100000, { \
              gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
        }) \
        gstats_handle_stat(LONG_LONG, millis_before_abort, 1, { \
              gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, millis_before_commit, 1, { \
              gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, spin_timer_duration, 1, {}) \
        gstats_handle_stat(LONG_LONG, mode2_spin_timer_duration_us, 1, { \
              gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, mode2_vltable_search_timer_duration, 1, {}) \
        gstats_handle_stat(LONG_LONG, mode2_vltable_search_timer_duration_us, 1, { \
              gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        \
        gstats_handle_stat(LONG_LONG, irrevocable_from_aborts, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, read_only_aborts, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, IRREVOCABLE_TXN, 1, { \
                gstats_output_item(PRINT_TO_FILE, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
          __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
        }) \
        gstats_handle_stat(LONG_LONG, RQ_START, 10000, { \
                gstats_output_item(PRINT_TO_FILE, NONE, FULL_DATA) \
        }) \
        gstats_handle_stat(LONG_LONG, RQ_END, 10000, { \
                gstats_output_item(PRINT_TO_FILE, NONE, FULL_DATA) \
        }) \
        gstats_handle_stat(LONG_LONG, num_update_only, 1, { \
                gstats_output_item(PRINT_RAW, SUM, BY_THREAD) \
                __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
    // define a variable for each stat above
    GSTATS_HANDLE_STATS_USER(__DECLARE_EXTERN_STAT_ID);
#endif
