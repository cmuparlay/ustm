/* =============================================================================
 *
 * platform.h
 *
 * Platform-specific bindings
 *
 * =============================================================================
 */


#ifndef PLATFORM_H
#define PLATFORM_H 1

#if defined(__sparc)
#  include "platform_sparc.h"
#elif defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__)
#  include "platform_p8.h"
#elif defined(__x86_64__) || defined(_M_X64)
#  include "platform_x86.h"
#else
#  error UNKNOWN platform
#endif

#define CAS(m,c,s)  cas((intptr_t)(s),(intptr_t)(c),(intptr_t*)(m))

typedef unsigned long long TL2_TIMER_T;


#endif /* PLATFORM_H */


/* =============================================================================
 *
 * End of platform.h
 *
 * =============================================================================
 */
