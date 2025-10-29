#pragma once

// Minimal user_gstats_handler.h for HyTm integration
// This provides stubs for the statistics functionality that HyTm expects

#ifndef GSTATS_HANDLE_STATS
// If GSTATS is not enabled, provide empty definitions
#define GSTATS_HANDLE_STATS_USER(gstats_handle_stat)

// Define empty stat variables to prevent linker errors
namespace {
  int phytm_wrset_size_commit = 0;
  int phytm_wrset_size_abort = 0;
  int phytm_rdset_size_commit = 0;
  int phytm_rdset_size_abort = 0;
  int phytm_stm_write_commits = 0;
  int phytm_stm_write_aborts_per_commit = 0;
  int phytm_stm_read_only_aborts_per_commit = 0;
  int total_commit = 0;
  int total_abort = 0;
  int stm_commit = 0;
  int stm_abort = 0;
  int htm_commit = 0;
  int htm_abort = 0;
}

#endif // GSTATS_HANDLE_STATS