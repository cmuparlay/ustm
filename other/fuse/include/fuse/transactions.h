#ifndef TLF_TRANSACTIONS_H
#define TLF_TRANSACTIONS_H

#include "flock/flock.h"
#include "flock/acquired_pool.h"
#include "../verlib/timestamps.h"

// defines the transaction_descriptor structure
#include "transaction_descriptor.h"

// memory pool to handle aborted allocates and retires
#include "memory_pool.h"

// the main functions 
#include "core_functions.h"

// The lock interface
#include "lock.h"

// The definition of atomic,
#include "atomic.h"

// The definition of versioned_ptr
#include "versioned_ptr.h"

// now expose the full interface in the verlib namespace
namespace verlib {
  using tlf_internal::memory_pool;
  using tlf_internal::atomic_region;
  using tlf_internal::atomic_read_only;
  using tlf_internal::lock;
  using tlf_internal::versioned;
  using tlf_internal::versioned_ptr;
  using tlf_internal::atomic_bool;
}




#endif // TLF_TRANSACTIONS_H
