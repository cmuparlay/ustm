#pragma once

#ifdef GSTATS_HANDLE_STATS
#   ifndef __AND
#      define __AND ,
#   endif
#   define GSTATS_HANDLE_STATS_USER(gstats_handle_stat) \
        gstats_handle_stat(LONG_LONG, phytm_wrset_size_commit, 10000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, phytm_wrset_size_abort, 10000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, phytm_rdset_size_commit, 10000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, phytm_rdset_size_abort, 10000, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, phytm_stm_write_commits, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
          __AND gstats_output_item(PRINT_RAW, SUM, BY_THREAD) \
        }) \
        gstats_handle_stat(LONG_LONG, phytm_stm_write_aborts_per_commit, 100, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
          __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, phytm_stm_read_only_aborts_per_commit, 100, { \
                gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
          __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
          __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
          __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, total_commit, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, total_abort, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, stm_commit, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, stm_abort, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, htm_commit, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \
        gstats_handle_stat(LONG_LONG, htm_abort, 1, { \
                gstats_output_item(PRINT_RAW, SUM, TOTAL) \
        }) \

    // define a variable for each stat above
    GSTATS_HANDLE_STATS_USER(__DECLARE_EXTERN_STAT_ID);
#endif
