// The core functions used by transactions.
//
// Includes the versioned struct, which is the meta information held
// on every link of a version list.  It contains a next_version
// pointer, and a timestamp/descriptor.  The timestamp/descriptor can
// be one of:
//   - a pointer to a transaction descriptor
//   - a valid timestamp
//   - a TBD timestamp
//   - a failed timestamp
//
// After taking locks during the commit phase of a transaction x, we
// use install_store for every store s in the write log.  This adds a
// new version link to s with next pointing to current contents of s,
// and the timestamp/descriptor is initialized to point to the
// transaction_descriptor of x.  At the end of the commit phase
// (before releasing locks), we use finalize_store to either (1) if
// succeeded, change the timestamp/descriptor to be the x's timestamp,
// or (2) if failed, remove the installed version link.
//
// TBD is used for singleton stores (not in a transaction).  In this
// case a new link is installed with a TBD timestamp, which is then
// changed to a real timestamp.
//
// The file also includes the get_value function, which safely reads a
// value from a location.  It needs to check the status of the latest
// version's meta data.  If the timestamp is valid, then the value can
// simply be returned, if the timestamp is failed, then the value of
// the next version is returned, and if the timestamp is TBD, the
// timestamp is set and the current value returned.  Otherwise (i.e.,
// a descriptor is installed) it has to check the status of the
// descriptor.  If the descriptor is pending or aborted then the value
// of the next version is returned, if the descriptor has succeeded
// then the current value is returned, and if the descriptor is
// validating then get_value will either abort or help the descriptor
// (depending which thread has a higher id).
//
// Since we use lock-free locks with flock, the install_store and
// finalize_store are idempotent---any number of threads can
// repeatedly apply them while helping the original thread.

#ifndef TLF_CORE_FUNCTIONS_H_
#define TLF_CORE_FUNCTIONS_H_

#include <utility>

namespace tlf_internal {
  using namespace verlib;

  bool is_helping() {
#ifndef FlockHelp
    return false;
#else
    return flck::internal::helping;
#endif
  }

  bool valid_pointer(void* ptr) { return ((size_t) ptr) < (1ul << 48);}

// initial value of next_version
#define init_ptr ((versioned*) 24ul)

  struct versioned {
  private:
    flck::atomic_aba_free<TS> stamp_or_descriptor;
    flck::atomic_write_once<versioned*> next_version;

    // tags second highest bit to mark as descriptor
    constexpr static int bit_num = 62;
    static TS tag_descriptor(transaction_descriptor* ptr) {
      return (TS) ptr | (1ul << bit_num);}
    
    static transaction_descriptor* untag_descriptor(TS sd) {
      auto ptr = (transaction_descriptor*) (sd & ~(1ul << bit_num));
      assert(valid_pointer((void*) ptr));
      return ptr;
    }

    static bool is_tagged(TS sd) {return (sd >> bit_num) & 1;}

  public:
    bool simple_stamp() { return stamp_or_descriptor.load() < failed;}
    bool has_descriptor() { return is_tagged(stamp_or_descriptor.load());}

    std::pair<transaction_descriptor*, TS> get_descriptor_and_stamp() {
      TS td = stamp_or_descriptor.load_ni();
      if (is_tagged(td)) {
              transaction_descriptor* descriptor = untag_descriptor(td);
              trans_descriptor_pool.acquire(descriptor);
              td = stamp_or_descriptor.load();
      } 
      if (is_tagged(td)) return std::pair(untag_descriptor(td), tbd);
      else return std::pair(nullptr, td);
    }

    void set_descriptor(transaction_descriptor* descriptor) {
      assert(stamp_or_descriptor.load() == tbd);
      stamp_or_descriptor.cas_ni(tbd, tag_descriptor(descriptor));
    }

    void remove_descriptor(TS ts, bool check_stamp = true) {
#ifndef NDEBUG
      TS td = stamp_or_descriptor.load();
#endif
      assert(is_tagged(td));
      assert(untag_descriptor(td)->owner == flck::internal::get_current_id());
      assert(!check_stamp || untag_descriptor(td)->time_stamp.load() == ts);
      //stamp_or_descriptor = ts;
      stamp_or_descriptor.store_ni(ts);
    }

    void set_zero_stamp() {
      // This gets run in a non-idempotent context (i.e., with no log), so needs to be
      // idempotent on its own.
      if (next_version.load_ni() == init_ptr) {
        // the order of the next two matter when using
        // with lock free locks.
        stamp_or_descriptor.store_ni(zero_stamp); // ok since write once
        next_version.store_ni(nullptr); // ok since write once
      }
    }

    // use when stamp is already set
    TS get_stamp() {
      auto [des, st] = get_descriptor_and_stamp();
      if (des == nullptr) return st;
      TS ts = des->time_stamp.load();
      assert(ts != tbd && ts != tbd && ts != failed);
      return ts;
    }

    // use when stamp could be descriptor
    TS get_stamp_indirect_ni() {
      TS td = stamp_or_descriptor.load_ni();
      if (is_tagged(td)) return tbd;
      else return td;
    }

    versioned() : stamp_or_descriptor(tbd), next_version(init_ptr) {} 
    versioned(versioned* next) : stamp_or_descriptor(tbd), next_version(next) {}

    versioned* get_next_version() {return next_version.load();}
    //void set_next_version(versioned* nv) { next_version = nv;}
    void init_next_version(versioned* nv) {
      //assert(next_version.load() == init_ptr);  
      next_version = nv;}

    // once a next version is added, it becomes recorded
    bool is_recorded() {
      return next_version.load() != init_ptr; }

    void set_with_current_stamp() {
      TS td = stamp_or_descriptor.load_ni();
      assert(!is_tagged(td));    
      if (td == tbd) {
        TS new_t = global_stamp.get_write_stamp();
        if (stamp_or_descriptor.load_ni() == tbd) 
          stamp_or_descriptor.cas_ni(tbd, new_t);
      }
    }
  };

  // uses lowest bit of pointer to indicate whether indirect (1) or not (0)
  versioned* add_indirect(versioned* ptr) {
    return (versioned*) (1ul | (size_t) ptr);};
  versioned* strip_indirect(versioned* ptr) {
    return (versioned*) ((size_t) ptr & ~1ul);}
  bool is_indirect(versioned* ptr) {
    return (size_t) ptr & 1;}

  struct ver_link : versioned {
    versioned* value;
    ver_link(versioned* next, versioned* value) : versioned{next}, value(value) {}
  };

  memory_pool<ver_link> link_pool;

  void shortcut(flck::atomic<versioned*>* loc, versioned* ptr) {
#ifndef NoShortcut
    ver_link* ptr_ = (ver_link*) strip_indirect(ptr);
    if (ptr_->get_stamp_indirect_ni() <= done_stamp) 
      if (loc->cas_ni(ptr, ptr_->value)) link_pool.pool.Retire(ptr_);
#endif
  }

  // Installs ptr to front of version list for a store on loc.
  // - If in a transaction, then set descriptor on the new link, and the
  // store will be completed by finalize_store.
  // - Otherwise set the timestamp and no finalization is required.
  std::pair<versioned*,versioned*>
  install_store(flck::atomic<versioned*>* loc,
                versioned* old_ptr,
                versioned* ptr,
                transaction_descriptor* descriptor) {
    assert(current_transaction == nullptr);
    auto [old_tag, old_v] = loc->load_linked();
    if (old_v != old_ptr) return std::pair<versioned*,versioned*>(nullptr, nullptr);
    versioned* new_v = ptr;
    // indirection on need
    bool use_indirect = (ptr == nullptr || ptr->is_recorded()); // logged
    if (use_indirect) {
      ver_link* link = link_pool.New(old_v, new_v); // logged
      link_pool.retire_on_abort(descriptor, link);
      new_v = add_indirect(link);
    }
    else ptr->init_next_version(old_v);
    
    // install descriptor if in transaction
    strip_indirect(new_v)->set_descriptor(descriptor);  // logged

#ifdef NoShortcut
    loc->store_conditional(old_tag, new_v);
    if (is_indirect(old_v))
      link_pool.retire_on_success(descriptor, (ver_link*) strip_indirect(old_v)); 
#else
    // if using shortcutting then need to deal with race
    if (is_indirect(old_v)) {
      loc->cam(old_v, new_v);  // logged
      versioned* val = loc->load(); // logged
      ver_link* old_l = (ver_link*) strip_indirect(old_v);
      if (val == old_l->value) { // if has been concurrently shortcut
        old_v = old_l->value;
        loc->cam(old_v, new_v); // logged, but unusal
      } else link_pool.retire_on_success(descriptor, old_l);
    } else loc->store_conditional(old_tag, new_v); // logged 
#endif
    return std::pair(old_v, new_v);
  }

  void nontransactional_store(flck::atomic<versioned*>* loc,
                              versioned* ptr) {
    assert(current_transaction == nullptr);
    auto [old_tag, old_v] = loc->load_linked();
    versioned* new_v = ptr;
    // indirection on need
    bool use_indirect = (ptr == nullptr || ptr->is_recorded()); // logged
    if (use_indirect) {
      ver_link* link = link_pool.New(old_v, new_v); // logged
      new_v = add_indirect(link);
    }
    else ptr->init_next_version(old_v);
    
#ifdef NoShortcut
    loc->store_conditional(old_tag, new_v);
    // *loc = new_v;
    if (is_indirect(old_v))
      link_pool.retire_on_success(nullptr, (ver_link*) strip_indirect(old_v)); 
#else
    // if using shortcutting then need to deal with race
    if (is_indirect(old_v)) {
      loc->cam(old_v, new_v);  // logged
      versioned* val = loc->load(); // logged
      ver_link* old_l = (ver_link*) strip_indirect(old_v);
      if (val == old_l->value) { // if has been concurrently shortcut
        old_v = old_l->value;
        loc->cam(old_v, new_v); // logged, but unusal
      } else link_pool.retire_on_success(nullptr, old_l);
    } else loc->store_conditional(old_tag, new_v); // logged 
#endif
    // set timestamp and try shortcut if not in transaction
      strip_indirect(new_v)->set_with_current_stamp();  // not logged
      if (use_indirect) shortcut(loc, new_v); // not logged
  }

  void finalize_store(flck::atomic<versioned*>* loc,
                                std::pair<versioned*,versioned*> pointers,
                                TS time_stamp) {
    assert(loc != nullptr);
    auto [old_ptr, new_ptr] = pointers;

    // splice this latest version out if transaction aborted
    if (time_stamp == failed) *loc = old_ptr; // logged

    // replace descriptor with timestamp (possibly failed stamp)
    strip_indirect(new_ptr)->remove_descriptor(time_stamp); 
  }

  void validate(transaction_descriptor* descriptor);

  thread_local int in_lock;
  thread_local bool abort_if_update;
  thread_local int try_count;

  // If a descriptor is installed then need to resolve whether to
  // return the new value (if succeded) or old value (if not yet in
  // validation stage or if failed).
  template <typename T>
  T resolve_descriptor(transaction_descriptor* descriptor, T old_v, T new_v,
                       bool validating, TS start_stamp) {
    if ((size_t) descriptor->owner == flck::internal::get_current_id()) {
      //assert(is_helping() || descriptor->round == transaction_round);
      assert(validating); // self descriptor should not be installed if not validating
      return old_v; // if validating then should check against old value
    }
    TS desc_ts = descriptor->time_stamp.load();
    if (desc_ts == tbd || desc_ts == failed) return old_v;
    if (start_stamp >= 0 && is_validating_stamped(desc_ts) &&
        start_stamp < get_validating_timestamp(desc_ts))
      return old_v;
    if (validating && is_validating_stamped(desc_ts) &&
        (size_t) descriptor->owner < flck::internal::get_current_id()) { 
      // kill other if they have lower id. prevents validate cycle
      for (volatile int i=0; i < 75; i++);
      descriptor->atomic_set_stamp(desc_ts, failed);
    } else { //otherwise wait for a while to see if other is done, then help if not
      int cnt = 0;
      desc_ts = descriptor->time_stamp.load();
      while (is_validating(desc_ts) && cnt++ < 50) {
        for (volatile int i=0; i < 100; i++);
        desc_ts = descriptor->time_stamp.load();
      }
      if (is_validating(desc_ts))
        validate(descriptor);
    }
    if (descriptor->time_stamp.load() == failed) return old_v;
    return new_v;
  }

  bool simple_validate(versioned* tagged_ptr, TS start_stamp) {
    if (tagged_ptr == nullptr) return true;
    versioned* ptr = strip_indirect(tagged_ptr);
    auto [descriptor, stamp] = ptr->get_descriptor_and_stamp();
    return (descriptor == nullptr && ptr->get_stamp() <= start_stamp);
  }

  versioned* get_value(versioned* tagged_ptr, bool validating = false, TS start_stamp = tbd) {
    if (tagged_ptr == nullptr) return nullptr;
    versioned* ptr = strip_indirect(tagged_ptr);
    auto [descriptor, stamp] = ptr->get_descriptor_and_stamp();
    if (stamp == failed) return ptr->get_next_version(); // failed so ignore value
    if (stamp != tbd) return tagged_ptr; // value already fully set
    if (descriptor == nullptr) { // store was not in a transaction
      ptr->set_with_current_stamp(); // check and set the stamp
      return tagged_ptr; }
    // a transaction descriptor is installed, need to resolve the state
    return resolve_descriptor(descriptor, ptr->get_next_version(),
                              tagged_ptr, validating, start_stamp);
  }

  bool validate_pointer(flck::atomic<versioned*>* location, versioned* expected, TS start_stamp) {
    versioned* ptr = get_value(location->load(), true, start_stamp);
    // second condition is to deal with shortcutting
    return (ptr == expected || (is_indirect(expected) &&
                                ptr == ((ver_link*) strip_indirect(expected))->value));
  }


} // namespace tlf_internal

#endif // TLF_CORE_FUNCTIONS_H_
