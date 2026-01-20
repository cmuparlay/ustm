#ifndef SPINS_H_GUARD
#define SPINS_H_GUARD

// #define FLUSH_X86_INST "clwb"
#define FLUSH_X86_INST "clflushopt"
// #define FLUSH_X86_INST "clflush"

#define FENCE_X86_INST "sfence"

// TODO: removed emulation
#if !defined(DISABLE_PMEM) && !defined(USE_NOOP_FLUSH_FENCE)
  #define FLUSH_CL(_addr) \
    __asm__ volatile(FLUSH_X86_INST " (%0)" : : "r"((void*)((uint64_t)(_addr) & -ARCH_CACHE_LINE_SIZE)) : "memory") \
  //
#else
  #define FLUSH_CL(_addr) //
#endif

#if !defined(DISABLE_PMEM) && !defined(USE_NOOP_FLUSH_FENCE)
  #define FENCE_PREV_FLUSHES() \
    __asm__ volatile(FENCE_X86_INST : : : "memory"); \
  //
#else
  #define FENCE_PREV_FLUSHES() //
#endif

// allow circular buffer
#if !defined(DISABLE_PMEM) && !defined(USE_NOOP_FLUSH_FENCE)
  #define FLUSH_RANGE(addr1, addr2, beginAddr, endAddr) \
    if (addr2 < addr1) { \
      for (uint64_t _addr = ((uint64_t)(addr1) & (uint64_t)-ARCH_CACHE_LINE_SIZE); \
                    _addr < (uint64_t)(endAddr); \
                    _addr += ARCH_CACHE_LINE_SIZE) { \
        __asm__ volatile(FLUSH_X86_INST " (%0)" : : "r"(((void*)_addr)) : "memory"); \
      } \
      for (uint64_t _addr = ((uint64_t)(beginAddr) & (uint64_t)-ARCH_CACHE_LINE_SIZE); \
                    _addr < (uint64_t)(addr2); \
                    _addr += ARCH_CACHE_LINE_SIZE) { \
        __asm__ volatile(FLUSH_X86_INST " (%0)" : : "r"(((void*)_addr)) : "memory"); \
      } \
    } else { \
      for (uint64_t _addr = ((uint64_t)(addr1) & (uint64_t)-ARCH_CACHE_LINE_SIZE); \
                    _addr < (uint64_t)(addr2); \
                    _addr += ARCH_CACHE_LINE_SIZE) { \
        __asm__ volatile(FLUSH_X86_INST " (%0)" : : "r"(((void*)_addr)) : "memory"); \
      } \
    } \
  //
#else
  #define FLUSH_RANGE(addr1, addr2, beginAddr, endAddr) //
#endif

#endif /* SPINS_H_GUARD */
