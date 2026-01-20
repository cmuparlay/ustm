#pragma once

#ifdef GSTATS_HANDLE_STATS
#   ifndef __AND
#      define __AND ,
#   endif

#   ifdef MEASURE_TIMELINE_STATS
#       define GSTATS_HANDLE_STATS_RECLAIMERS_WITH_EPOCHS(gstats_handle_stat) \
                gstats_handle_stat(LONG_LONG, limbo_reclamation_event_size, 10, { \
                        gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
                  __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
                  __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
                  __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
                }) \
                gstats_handle_stat(LONG_LONG, limbo_reclamation_event_count, 1, { \
                        gstats_output_item(PRINT_RAW, SUM, TOTAL) \
                }) \
                gstats_handle_stat(LONG_LONG, limbo_object_frees, 1, { \
                        gstats_output_item(PRINT_RAW, SUM, TOTAL) \
                }) \
                gstats_handle_stat(LONG_LONG, timersplit_epoch, 1, {}) \
                gstats_handle_stat(LONG_LONG, timersplit_token_received, 1, {}) \
                gstats_handle_stat(LONG_LONG, timer_bag_rotation_start, 1, {}) \
                gstats_handle_stat(LONG_LONG, thread_announced_epoch, 1, { \
                        gstats_output_item(PRINT_RAW, FIRST, BY_THREAD) \
                }) \
                gstats_handle_stat(LONG_LONG, freelist_size_in_epoch, 10, {}) \
                gstats_handle_stat(LONG_LONG, limbobag_size_in_epoch, 10, {}) \
                gstats_handle_stat(LONG_LONG, timeline_freeOne, 99999, { \
                        gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
                  __AND gstats_output_item(PRINT_RAW, MAX, TOTAL) \
                  __AND gstats_output_item(PRINT_TO_FILE, NONE, FULL_DATA) \
                }) \
                gstats_handle_stat(LONG_LONG, timeline_rotateEpochBags, 99999, { \
                        gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
                  __AND gstats_output_item(PRINT_RAW, MAX, TOTAL) \
                  __AND gstats_output_item(PRINT_TO_FILE, NONE, FULL_DATA) \
                }) \
                gstats_handle_stat(LONG_LONG, blip_advanceEpoch, 100000, { \
                        gstats_output_item(PRINT_TO_FILE, NONE, FULL_DATA) \
                }) \
                gstats_handle_stat(LONG_LONG, garbage_in_epoch, 25000, { \
                        gstats_output_item(PRINT_RAW, COUNT, BY_INDEX) \
                  __AND gstats_output_item(PRINT_RAW, AVERAGE, BY_INDEX) \
                })
#   else
#       define GSTATS_HANDLE_STATS_RECLAIMERS_WITH_EPOCHS(gstats_handle_stat) \
                gstats_handle_stat(LONG_LONG, limbo_reclamation_event_size, 10, { \
                        gstats_output_item(PRINT_HISTOGRAM_LOG, NONE, FULL_DATA) \
                  __AND gstats_output_item(PRINT_RAW, SUM, TOTAL) \
                  __AND gstats_output_item(PRINT_RAW, COUNT, BY_THREAD) \
                  __AND gstats_output_item(PRINT_RAW, COUNT, TOTAL) \
                }) \
                gstats_handle_stat(LONG_LONG, limbo_reclamation_event_count, 1, { \
                        gstats_output_item(PRINT_RAW, SUM, TOTAL) \
                }) \
                gstats_handle_stat(LONG_LONG, limbo_object_frees, 1, { \
                        gstats_output_item(PRINT_RAW, SUM, TOTAL) \
                }) \
                gstats_handle_stat(LONG_LONG, timersplit_epoch, 1, {}) \
                gstats_handle_stat(LONG_LONG, timersplit_token_received, 1, {}) \
                gstats_handle_stat(LONG_LONG, timer_bag_rotation_start, 1, {}) \
                gstats_handle_stat(LONG_LONG, thread_announced_epoch, 1, { \
                        gstats_output_item(PRINT_RAW, FIRST, BY_THREAD) \
                }) \
                gstats_handle_stat(LONG_LONG, freelist_size_in_epoch, 10, {}) \
                gstats_handle_stat(LONG_LONG, limbobag_size_in_epoch, 10, {}) \
                gstats_handle_stat(LONG_LONG, timeline_freeOne, 99999, {}) \
                gstats_handle_stat(LONG_LONG, timeline_rotateEpochBags, 99999, {}) \
                gstats_handle_stat(LONG_LONG, blip_advanceEpoch, 35000, {}) \
                gstats_handle_stat(LONG_LONG, garbage_in_epoch, 25000, {})
#   endif

    // define a variable for each stat above
    GSTATS_HANDLE_STATS_RECLAIMERS_WITH_EPOCHS(__DECLARE_EXTERN_STAT_ID);

#   define GSTATS_CLEAR_TIMERS \
        GSTATS_CLEAR_VAL(timersplit_epoch, get_server_clock()); \
        GSTATS_CLEAR_VAL(timersplit_token_received, get_server_clock()); \
        GSTATS_CLEAR_VAL(timer_bag_rotation_start, get_server_clock());

#endif
