// Code for hardware timeStamp.
// This works on intel machines, but not others.
// Currently not used

namespace ustm {
struct timeStamp {
    TS getStamp() {
      unsigned int lo,hi;
      __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
      return (TS) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
    }
    TS nextStamp() { return getStamp(); }
  };
}
