/* =============================================================================
 *
 * stm.h
 *
 * User program interface for STM. For an STM to interface with STAMP, it needs
 * to have its own stm.h for which it redefines the macros appropriately.
 *
 * =============================================================================
 *
 * Author: Chi Cao Minh
 *
 * HEAVY EDITS BY TREVOR BROWN 2020
 *
 * =============================================================================
 */
#pragma once
#include "tl2.h"
#include "util.h"
// #include "../hytm1/counters/counters.h"
#include "../../tmlib/hytm1/counters/counters.h"

#define STM_THREAD_T                    Thread
#define STM_SELF                        Self
#define STM_RO_FLAG                     ROFlag

#define STM_MALLOC(stm_self, size)      TxAlloc((Thread *)(stm_self), size)
#define STM_FREE(stm_self, ptr)         TxFree((Thread *)(stm_self), ptr)

#include <setjmp.h>
#define STM_JMPBUF_T                    sigjmp_buf
#define STM_JMPBUF                      buf

#define STM_CLEAR_COUNTERS()            /* nothing */
#define STM_VALID()                     (1)
#define STM_RESTART(stm_self)           TxAbort((Thread *)(stm_self))

#define STM_STARTUP()                   TxOnce()
#define STM_SHUTDOWN()                  TxShutdown()

#define STM_NEW_THREAD(id)              TxNewThread()
#define STM_INIT_THREAD(t, id)          TxInitThread((Thread *)(t), id)
#define STM_FREE_THREAD(t)              TxFreeThread((Thread *)(t))

typedef volatile intptr_t               vintp;
#  define STM_BEGIN(isReadOnly, stm_self) \
    do { \
        STM_JMPBUF_T STM_JMPBUF; \
        int STM_RO_FLAG = isReadOnly; \
        sigsetjmp(STM_JMPBUF, 0); \
        TxStart((stm_self), &STM_JMPBUF, &STM_RO_FLAG); \
    } while (0) /* enforce comma */
#define STM_BEGIN_RD(stm_self)          STM_BEGIN(1, (Thread *)(stm_self))
#define STM_BEGIN_WR(stm_self)          STM_BEGIN(0, (Thread *)(stm_self))
#define STM_END(stm_self)               TxCommit((Thread *)(stm_self))
#define STM_READ_P(stm_self, var)       IP2VP(TxLoad((Thread *)(stm_self), (vintp*)(void*)&(var)))
#define STM_WRITE_P(stm_self, var, val) TxStore((Thread *)(stm_self), (vintp*)(void*)&(var), VP2IP(val))
