# NV-HALT Integration Guide

## Summary
This document summarizes the analysis of NV-HALT (from ptmbench) for integration into other STM benchmark suites.

## What is NV-HALT?
- **Hybrid Transactional Memory** with persistence support
- **Based on HyTM1** with added NVRAM capabilities  
- **Two-path execution**: Hardware transactions → Software STM fallback
- **Persistence layer**: Write-through logging to NVRAM

## Key Source Files

### Core API (Single Header Entry Point)
```
phytmlib/phytm2.h                    # Main API - single include needed
```

### Core Implementation Files
```
phytmlib/
├── phytm2.h                         # Main API wrapper
├── tx_tuple.h                       # Transactional data structures
├── phytmCommon/
│   ├── tm.h                         # Core STM implementation (2000+ lines)
│   ├── stmCommon.h                  # STM utilities  
│   ├── config.h                     # Configuration parameters
│   ├── common.h                     # Platform abstractions
│   ├── stmUtil.h                    # Utility functions
│   ├── tm_memory_manager.h          # Memory management
│   ├── nvram_file_mapping.h         # NVRAM file operations
│   ├── htmWriteSetEntry.h           # HTM write set structures
│   └── abortTypes.h                 # Abort type definitions
└── volatile_phytm/
    ├── stm_volatile.h               # Volatile-only STM (for comparisons)
    └── tm_volatile.h                # Volatile TM wrapper
```

### External Dependencies
```
tmlib/hytm1/                         # HyTM1 platform support (REQUIRED)
├── platform.h                      # Platform detection
├── platform_impl.h                 # Platform implementation  
├── platform_impl_x86.h             # x86-specific code
├── platform_x86.h                  # x86 platform bindings
├── rtm_x86.h                        # x86 HTM instructions
├── common.h                         # Common utilities
└── counters/counters.h              # Performance counters

user_gstats_handler.h                # Statistics configuration (project root)
```

### System Dependencies
- **libnuma-dev**: NUMA memory management library
- **Standard C++17**: No other external libraries

## API Overview

### C API (Low-level)
```c
// System management
TM_STARTUP()                         // Initialize system
TM_SHUTDOWN()                        // Shutdown system
TM_THREAD_ENTER()                    // Per-thread init
TM_THREAD_EXIT()                     // Per-thread cleanup

// Transaction control  
TM_BEGIN()                           // Start read-write transaction
TM_BEGIN_RO()                        // Start read-only transaction
TM_BEGIN_UNSAFE()                    // Start unsafe mode
TM_END()                             // End and commit transaction
TM_END_UNSAFE()                      // End unsafe mode
TM_RESTART()                         // Abort transaction

// Memory operations
TM_SHARED_READ_P(var)                // Transactional read
TM_SHARED_WRITE_P(var, val)          // Transactional write

// Memory management
TM_MALLOC(size)                      // Transactional allocation
TM_FREE(ptr)                         // Transactional free
```

### C++ API (Recommended)
```cpp
// Transactional fields (preferred interface)
template <typename T>
struct tx_field {
    T load()                         // Explicit read
    T store(T value)                 // Explicit write
    operator T()                     // Implicit read via cast
    T operator=(T value)             // Assignment operator
    T load_unsafe()                  // Bypass STM (unsafe mode)
    T store_unsafe(T value)          // Bypass STM (unsafe mode)
};

// Memory management
template <typename T, typename... Args> 
static T* tmNew(Args&&... args);     // Transactional new
template<typename T> 
static void tmDelete(T* obj);        // Transactional delete
static void* tmMalloc(size_t size);  // C-style malloc
static void tmFree(void* obj);       // C-style free
```

## Configuration for NVRAM Disabled

### Option 1: Compile-time Disable
```cpp
#define DISABLE_PMEM                 // In config.h or build flags
```

### Option 2: Use Pre-built Variants
```
abtree.none.phytm1_fgl_colocated_locks_no_pmem           # NVRAM completely disabled
abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence  # Flush/fence disabled
abtree.none.phytm1_fgl_colocated_locks_no_phtm          # Fully volatile
```

## Usage Example

### C++ Style (Recommended)
```cpp
#include "phytm2.h"

struct Node {
    tx_field<int> value;
    tx_field<Node*> next;
    Node(int val) : value(val), next(nullptr) {}
};

int main() {
    // Initialize
    TM_STARTUP();
    TM_THREAD_ENTER();
    
    // Use transactions
    TM_BEGIN();
    Node* node = tmNew<Node>(42);
    node->value = 100;              // Uses operator=
    int val = node->value;          // Uses operator T()
    tmDelete(node);
    TM_END();
    
    // Cleanup
    TM_THREAD_EXIT();
    return 0;
}
```

### Integration with Benchmark Suite
```cpp
// In your adapter class
class stm_adapter {
    void initThread(int tid) {
        TM_THREAD_ENTER();
    }
    
    void deinitThread(int tid) {
        TM_THREAD_EXIT(); 
    }
    
    bool insert(int key, int value) {
        TM_BEGIN();
        bool result = datastructure->insert(key, value);
        TM_END();
        return result;
    }
};
```

## Key Insights

1. **Header-Only**: NV-HALT is essentially header-only implementation
2. **HyTM1 Dependency**: Requires complete tmlib/hytm1/ directory  
3. **C++ Preferred**: tx_field<T> template interface is cleaner than C macros
4. **NVRAM Optional**: Can be completely disabled for pure STM comparison
5. **Platform Support**: Currently supports x86-64 with HTM

## For Your Integration

### Minimum Files Needed
1. Copy entire `phytmlib/` directory
2. Copy entire `tmlib/hytm1/` directory
3. Copy `user_gstats_handler.h` (or create minimal stub)
4. Install libnuma-dev: `sudo apt-get install libnuma-dev`

### Alternative (Minimal)
- Create stub versions of external dependencies
- Disable NUMA support in config  
- Use simplified statistics handler
- Focus on volatile variants for pure STM comparison

## Available Variants

- **NV-HALT**: Full persistence with NVRAM
- **NV-HALT-CL**: With colocated locks
- **NV-HALT-NOOPF**: No-op flush/fence operations
- **NV-HALT-NO-PMEM**: NVRAM completely disabled
- **NV-HALT-VOLATILE**: Fully volatile (closest to pure STM)

## Build Integration

The build uses compile-time flags to select variants:
```makefile
# For NVRAM disabled
CPPFLAGS += -DDISABLE_PMEM

# For specific variants  
CPPFLAGS += -Dphytm1_fgl_colocated_locks_no_pmem
```

## Performance Notes

- HTM attempts threshold: 40 (configurable)
- Supports weak and strong progressiveness  
- Write-set sorting available for contention handling
- Memory management optimized for transaction commit/abort