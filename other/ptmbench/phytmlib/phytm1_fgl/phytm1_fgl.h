#pragma once

//#ifdef __cplusplus
//extern "C" {
//#endif

#define TM_NAME "phytm1_fgl"

// #include "../phytmCommon/common.h"

#ifdef USE_GLOBAL_LOCK
#undef USE_GLOBAL_LOCK
#endif

#include "../phytmCommon/tm.h"

//#ifdef __cplusplus
//}
//#endif
