/**
 * This is a variant of token EBR described in reclaimer_token1.h
 * Copyright (C) 2023 Trevor Brown
 */

#pragma once

#if !defined vtoken2
#   define vtoken2
#endif
#include "reclaimer_token1.h"

template <typename T = void, class Pool = pool_interface<T> >
class reclaimer_token2 : public reclaimer_token1<T, Pool> {
    reclaimer_token2(const int numProcesses, Pool *_pool, debugInfo * const _debug, RecoveryMgr<void *> * const _recoveryMgr = NULL)
            : reclaimer_token1<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr) {}
};
