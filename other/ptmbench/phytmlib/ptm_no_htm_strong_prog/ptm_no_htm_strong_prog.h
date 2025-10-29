#pragma once

//#ifdef __cplusplus
//extern "C" {
//#endif

#define TM_NAME "ptm-no-htm-strong-prog"
#ifndef DISABLE_HTM
	#define DISABLE_HTM
#endif
#define ENABLE_WRITE_SET_SORTING 1
#define STRONG_PROG 1

#include "../phytmCommon/tm.h"

//#ifdef __cplusplus
//}
//#endif
