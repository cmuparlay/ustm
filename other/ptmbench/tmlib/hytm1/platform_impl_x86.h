/* =============================================================================
 *
 * platform_x86.h
 *
 * x86-specific bindings
 *
 * =============================================================================
 */


#ifndef PLATFORM_IMPL_X86_H
#define PLATFORM_IMPL_X86_H 1

#ifndef PLATFORM_IMPL_H
#  error include "platform_impl.h" instead
#endif

#include <stdint.h>
#include "rtm_x86.h"
#include "common.h"

#define SYS_X86_64

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
inline intptr_t
cas (intptr_t newVal, intptr_t oldVal, volatile intptr_t* ptr)
{
    return __sync_val_compare_and_swap(ptr, oldVal, newVal);
}

/* =============================================================================
 * Prefetching
 *
 * We use PREFETCHW in LD...CAS and LD...ST circumstances to force the $line
 * directly into M-state, avoiding RTS->RTO upgrade txns.
 * =============================================================================
 */
#ifndef ARCH_HAS_PREFETCHW
inline void
prefetchw (volatile void* x)
{
    /* nothing */
}
#endif

#endif 