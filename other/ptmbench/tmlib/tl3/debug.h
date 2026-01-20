#pragma once

#include "../hytm1/platform.h"
#include "../tm.h"


// just for debugging
PAD;
volatile int globallock = 0;
PAD;

//#define DEBUG_PRINT
#define DEBUG_PRINT_LOCK

#ifdef DEBUG_PRINT
    #define aout(x) { \
        cout<<x<<endl; \
    }
#elif defined(DEBUG_PRINT_LOCK)
    #define aout(x) { \
        __acquireLock(&globallock); \
        cout<<x<<endl; \
        __releaseLock(&globallock); \
    }
#else
    #define aout(x)
#endif

#define debug(x) (#x)<<"="<<x
//#define LONG_VALIDATION
#define ERROR(x) { \
    cerr<<"ERROR: "<<x<<endl; \
    printStackTrace(); \
    exit(-1); \
}

#include <map>
PAD;
std::map<const void*, unsigned> addrToIx;
std::map<unsigned, const void*> ixToAddr;
volatile unsigned rename_ix = 0;
PAD;
#include <sstream>
std::string stringifyIndex(size_t ix) {
#if 1
    const unsigned NCHARS = 36;
    std::stringstream ss;
    if (ix == 0) return "0";
    while (ix > 0) {
        unsigned newchar = ix % NCHARS;
        if (newchar < 10) {
            ss<<(char)(newchar+'0');
        } else {
            ss<<(char)((newchar-10)+'A');
        }
        ix /= NCHARS;
    }
    std::string backwards = ss.str();
    std::stringstream ssr;
    for (std::string::reverse_iterator rit = backwards.rbegin(); rit != backwards.rend(); ++rit) {
        ssr<<*rit;
    }
    return ssr.str();
#elif 0
    const unsigned NCHARS = 26;
    std::stringstream ss;
    if (ix == 0) return "0";
    while (ix > 0) {
        unsigned newchar = ix % NCHARS;
        ss<<(char)(newchar+'A');
        ix /= NCHARS;
    }
    return ss.str();
#else
    std::stringstream ss;
    ss<<ix;
    return ss.str();
#endif
}
std::string renamePointer(const void* p) {
    DEBUG3 {
        std::map<const void*, unsigned>::iterator it = addrToIx.find(p);
        if (it == addrToIx.end()) {
            unsigned newix = __sync_fetch_and_add(&rename_ix, 1);
            addrToIx[p] = newix;
            ixToAddr[newix] = p;
            return stringifyIndex(addrToIx[p]);
        } else {
            return stringifyIndex(it->second);
        }
    } else {
        return stringifyIndex((size_t) (uintptr_t) p);
    }
}


PAD;
volatile long StartTally = 0;
volatile long AbortTallyHW = 0;
volatile long AbortTallySW = 0;
volatile long CommitTallyHW = 0;
volatile long CommitTallySW = 0;
PAD;


void printStackTrace() {

  void *trace[16];
  char **messages = (char **)NULL;
  int i, trace_size = 0;

  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  /* skip first stack frame (points here) */
  printf("  [bt] Execution path:\n");
  for (i=1; i<trace_size; ++i)
  {
    printf("    [bt] #%d %s\n", i, messages[i]);

    /**
     * find first occurrence of '(' or ' ' in message[i] and assume
     * everything before that is the file name.
     */
    int p = 0; //size_t p = 0;
    while(messages[i][p] != '(' && messages[i][p] != ' '
            && messages[i][p] != 0)
        ++p;

    char syscom[256];
    sprintf(syscom,"echo \"    `addr2line %p -e %.*s`\"", trace[i], p, messages[i]);
        //last parameter is the file name of the symbol
    if (system(syscom) < 0) {
        printf("ERROR: could not run necessary command to build stack trace\n");
        exit(-1);
    };
  }

  exit(-1);
}

void initSighandler() {
    /* Install our signal handler */
    struct sigaction sa;

    sa.sa_handler = (sighandler_t) /*(void *)*/ printStackTrace;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
}

/** used only for debugging **/
void __acquireLock(volatile int *lock) {
    while (1) {
        if (*lock) {
            PAUSE();
            continue;
        }
        LWSYNC; // prevent the following CAS from being moved before read of lock (on power)
        if (__sync_bool_compare_and_swap(lock, 0, 1)) {
            SYNC_RMW; // prevent instructions in the critical section from being moved before the lock (on power)
            return;
        }
    }
}

/** used only for debugging **/
void __releaseLock(volatile int *lock) {
    LWSYNC; // prevent unlock from being moved before instructions in the critical section (on power)
    *lock = 0;
}



