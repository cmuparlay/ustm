/* =============================================================================
 *
 * platform_x86.h
 *
 * x86-specific bindings
 *
 * =============================================================================
 */


#ifndef PLATFORM_X86_H
#define PLATFORM_X86_H 1

#ifndef PLATFORM_H
#  error include "platform.h" for "platform_x86.h"
#endif

#include <stdint.h>
#include "rtm_x86.h"
#include "common.h"


/* =============================================================================
 * Compare-and-swap
 *
 * CCM: Notes for implementing CAS on x86:
 *
 * /usr/include/asm-x86_64/system.h
 * http://www-128.ibm.com/developerworks/linux/library/l-solar/
 * http://gcc.gnu.org/onlinedocs/gcc-4.1.0/gcc/Atomic-Builtins.html#Atomic-Builtins
 *
 * In C, CAS would be:
 *
 * static __inline__ intptr_t cas(intptr_t newv, intptr_t old, intptr_t* ptr) {
 *     intptr_t prev;
 *     pthread_mutex_lock(&lock);
 *     prev = *ptr;
 *     if (prev == old) {
 *         *ptr = newv;
 *     }
 *     pthread_mutex_unlock(&lock);
 *     return prev;
 * =============================================================================
 */
intptr_t cas (intptr_t newVal, intptr_t oldVal, volatile intptr_t* ptr);


/* =============================================================================
 * Memory Barriers
 * =============================================================================
 */

#define LWSYNC /* not needed */
#define SYNC __sync_synchronize()
#define SYNC_RMW /* not needed */

/* =============================================================================
 * Prefetching
 *
 * We use PREFETCHW in LD...CAS and LD...ST circumstances to force the $line
 * directly into M-state, avoiding RTS->RTO upgrade txns.
 * =============================================================================
 */
#ifndef ARCH_HAS_PREFETCHW
void prefetchw (volatile void* x);
#endif


/* =============================================================================
 * Non-faulting load
 * =============================================================================
 */
#define LDNF(a)                         (*(a)) /* CCM: not yet implemented */


/* =============================================================================
 * MP-polite spinning
 *
 * Ideally we would like to drop the priority of our CMT strand.
 * =============================================================================
 */
#define PAUSE()                         __asm__ __volatile__("pause;")


/* =============================================================================
 * Timer functions
 * =============================================================================
 */
/* CCM: low overhead timer; also works with simulator */
#define TL2_TIMER_READ() ({ \
    unsigned int lo; \
    unsigned int hi; \
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi)); \
    ((TL2_TIMER_T)hi) << 32 | lo; \
})

#endif /* PLATFORM_X86_H */


/* =============================================================================
 *
 * End of platform_x86.h
 *
 * =============================================================================
 */
