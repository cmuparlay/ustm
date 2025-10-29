#ifndef RDTSC_H_GUARD
#define RDTSC_H_GUARD

#include <stdint.h>


#ifdef __cplusplus
extern "C"
{
#endif

#if defined(__i386__)

static __inline__ unsigned long long rdtsc(void) {
    unsigned long long int x;
    __asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
    return x;
}
#elif defined(__x86_64__)

#define rdtsc(void) ({ \
    unsigned long long _res; \
    __asm__ __volatile__ ( \
		"lfence;         \n\t" \
    "xor %%rax,%%rax \n\t" \
		"rdtsc           \n\t" \
		"shl $32,%%rdx   \n\t" \
		"or  %%rax,%%rdx \n\t" \
		"mov %%rdx,%0    \n\t" \
		: "=r"(_res) \
		: \
		: "rax", "rdx"); \
    _res; \
})

#define rdtscp(void) ({ \
    unsigned long long _res; \
    __asm__ __volatile__ ( \
		"xor %%rax,%%rax \n\t" \
		"rdtscp          \n\t" \
		"shl $32,%%rdx   \n\t" \
		"or  %%rax,%%rdx \n\t" \
		"mov %%rdx,%0    \n\t" \
		: "=r"(_res) \
		: \
		: "rax", "rdx", "ecx"); \
    _res; \
})

// static __inline__ uint64_t rdtsc() {
//   unsigned hi, lo;
//   __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
//   uint64_t ret = ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
//   return ret;
// }

#elif defined(__powerpc__)

static __inline__ unsigned long long rdtsc(void) {
    unsigned long long int result = 0;
    unsigned long int upper, lower, tmp;
    __asm__ volatile(
            "0:               \n\t"
            "mftbu   %0       \n\t"
            "mftb    %1       \n\t"
            "mftbu   %2       \n\t"
            "cmpw    %2,%0    \n\t"
            "bne     0b"
            : "=r"(upper), "=r"(lower), "=r"(tmp)
            );
    result = upper;
    result = result << 32;
    result = result | lower;

    return (result);
}

#else

#error "No tick counter is available!"

#endif

#ifdef __cplusplus
}
#endif

#endif /* RDTSC_H_GUARD */
