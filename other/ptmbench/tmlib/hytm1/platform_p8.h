/* =============================================================================
 *
 * platform_x86.h
 *
 * x86-specific bindings
 *
 * =============================================================================
 */


#ifndef PLATFORM_P8_H
#define PLATFORM_P8_H 1

#ifndef PLATFORM_H
#  error include "platform.h" for "platform_p8.h"
#endif

#include <stdint.h>
#include "rtm_p8.h"
#include "common.h"

#define SYS_POWER8


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


#define __ldarx(base) 		\
  ({unsigned long long result;	       			\
    typedef  struct {char a[8];} doublewordsize;	\
    doublewordsize *ptrp = (doublewordsize*)(void*)(base);	\
  __asm__ volatile ("ldarx %0,%y1"			\
	   : "=r" (result)				\
	   : "Z" (*ptrp));				\
result; })

#define __stdcx(base, value) 	\
  ({unsigned long long result;				\
    typedef  struct {char a[8];} doublewordsize;	\
    doublewordsize *ptrp = (doublewordsize*)(void*)(base);	\
  __asm__ volatile ("stdcx. %2,%y1\n"			\
	   "\tmfocrf %0,0x80"				\
	   : "=r" (result),				\
	     "=Z" (*ptrp)				\
	   : "r" (value) : "cr0");			\
((result & 0x20000000) >> 29); })


/* =============================================================================
 * Memory Barriers
 * =============================================================================
 */

#ifndef __TM_FENCE__
#   error __TM_FENCE__ must be defined for POWER systems: use an up to date version of GCC
#endif

#define LWSYNC asm volatile("lwsync" ::: "memory")
#define SYNC asm volatile("sync" ::: "memory")
#define SYNC_RMW asm volatile("sync" ::: "memory")

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
#define PAUSE()                         /* nothing */




#endif /* PLATFORM_P8_H */
