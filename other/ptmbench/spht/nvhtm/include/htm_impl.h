#ifndef HTM_IMPL_H_GUARD
#define HTM_IMPL_H_GUARD

// before include
//#define HTM_SGL_INIT_BUDGET /* default */20

#define HTM_SGL_INIT_BUDGET 10
#include "htm_retry_template.h"

#undef AFTER_ABORT
#define AFTER_ABORT(tid, budget, status) \
  MEASURE_TS(timeAbortedTX_TS2); \
  INC_PERFORMANCE_COUNTER(timeAbortedTX_TS1, timeAbortedTX_TS2, timeAbortedTX); \
  timeAbortedTX_TS1 = timeAbortedTX_TS2; \
  on_htm_abort(tid)

#undef BEFORE_SGL_BEGIN
#ifdef PROFILE_FALLBACK_PATH_TIME
  #define BEFORE_SGL_BEGIN(HTM_SGL_tid) A_MEASURE_TS(timeSGL_TS1);
#else
  #define BEFORE_SGL_BEGIN(HTM_SGL_tid) MEASURE_TS(timeSGL_TS1);
#endif

#undef AFTER_SGL_COMMIT
#ifdef PROFILE_FALLBACK_PATH_TIME
  #define AFTER_SGL_COMMIT(HTM_SGL_tid) \
    A_MEASURE_TS(timeSGL_TS2); \
    A_INC_PERFORMANCE_COUNTER(timeSGL_TS1, timeSGL_TS2, timeSGL);
#else 
  #define AFTER_SGL_COMMIT(HTM_SGL_tid) \
    MEASURE_TS(timeSGL_TS2); \
    INC_PERFORMANCE_COUNTER(timeSGL_TS1, timeSGL_TS2, timeSGL);
#endif


#undef BEFORE_CHECK_BUDGET
#define BEFORE_CHECK_BUDGET(HTM_SGL_budget) MEASURE_TS(timeAbortedTX_TS1);

#endif /* HTM_IMPL_H_GUARD */
