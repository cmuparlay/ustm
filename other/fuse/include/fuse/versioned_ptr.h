// Definition of versioned_ptr and atomic_bool

#ifndef TLF_VERSIONED_POINTER_H_
#define TLF_VERSIONED_POINTER_H_

namespace tlf_internal {

#include <optional>
#include <tuple>
#include <utility>
  
  template <typename V>
  struct versioned_ptr {
    //  private:
    flck::atomic<versioned*> v;

    V* get_ptr_shortcut(versioned* ptr) {
      versioned* ptr_ = strip_indirect(ptr);
      if (!is_indirect(ptr)) return (V*) ptr_;
      shortcut(&v, ptr);
      return (V*) ((ver_link*) ptr_)->value;
    }

    static versioned* set_zero_stamp(V* ptr) {
      if (ptr != nullptr) ptr->set_zero_stamp();
      return ptr;
    }

    // Only used by load_transactional.
    // Returns the value of the version of "ptr" at time stamp "at_stamp".
    // If the most recent version is ahead of at_stamp, and in_lock is
    // set, then sets "abort_if_update" to true.
    V* get_version(versioned* ptr, TS at_stamp) {
      if (is_indirect(ptr)) shortcut(&v, ptr);
      versioned* ptr_ = strip_indirect(ptr);
      if (ptr != nullptr && ptr_->get_stamp() > at_stamp) {
	if (in_lock > 0) abort_if_update = true;
	// chase down version chain
	do {
	  ptr = ptr_->get_next_version();
	  ptr_ = strip_indirect(ptr);
	} while (ptr != nullptr && ptr_->get_stamp() > at_stamp);
      }
      if (!is_indirect(ptr)) return (V*) ptr;
      else return (V*) ((ver_link*) ptr_)->value;
    }

  public:
    // Load when in a transaction (only used by load)
    V* load_transactional() {
      // first see if it is in the write log

      if (current_transaction->write_ptr_log.size() > 0) {
        std::optional<int> lid = current_transaction->find_in_write_log(v);
        if (lid.has_value())
          return (V*) std::get<2>(current_transaction->write_ptr_log[*lid]);
      }
      
      // if not then get the value and add to validate log if in a lock
      versioned* ptr = get_value(v.load_ni(), false, start_stamp);
      if (in_lock > 0) 
	current_transaction->validate_ptr_log.add(std::tuple(&v, ptr));

      // returns version at start_stamp for opacity
      return get_version(ptr, start_stamp);
    }

  public: // The public interface
    
    versioned_ptr(): v(nullptr) {}
    versioned_ptr(V* ptr) : v(set_zero_stamp(ptr)) {}

    ~versioned_ptr() {
      versioned* ptr = v.load();
      if (is_indirect(ptr))
	link_pool.Delete((ver_link*) strip_indirect(ptr));
    }

    void init(V* ptr) {v = set_zero_stamp(ptr);}

    // eventually move to private
    V* read_snapshot() {
      TS ls = local_stamp;
      versioned* ptr = get_value(v.read(), false, ls);
      versioned* ptr_ = strip_indirect(ptr);

      // chase down version chain
      while (ptr != nullptr && ptr_->get_stamp() > ls) {
	ptr = ptr_->get_next_version();
	ptr_ = strip_indirect(ptr);
      }
#ifdef LazyStamp
      if (ptr != nullptr && ptr_->get_stamp() == ls
	  && speculative)
	aborted = true;
#endif
      if (is_indirect(ptr)) {
	return (V*) ((ver_link*) ptr_)->value;
      } else return (V*) ptr;
    }

    V* load() {
      assert(!is_helping() || current_transaction == nullptr);
      if (local_stamp != -1)  return read_snapshot();
      if (current_transaction == nullptr) {
	versioned* ptr = v.load();
	// skip get_value in common case
	if (ptr != nullptr && !strip_indirect(ptr)->simple_stamp())
	  ptr = get_value(ptr);
	return get_ptr_shortcut(ptr);
      } else return load_transactional();
    }

    void validate() {
      if (current_transaction != nullptr) {
        versioned* ptr = get_value(v.load_ni(), false, local_stamp);
        if (ptr != nullptr && strip_indirect(ptr)->get_stamp() > start_stamp)
          abort_if_update = true;
        else current_transaction->validate_ptr_log.add(std::tuple(&v, ptr));
      }
    }

    void store(V* ptr) {
      if (in_constructor) init(ptr);
      else if (current_transaction != nullptr) {
	current_transaction->add_to_write_log(v, get_value(v.load_ni(), false, local_stamp), ptr);
      } else { // not in a transaction
	nontransactional_store(&v, ptr);
      }
    }
    V* operator=(V* b) {store(b); return b; }
  };

  struct Empty : versioned {
    Empty() {versioned::set_zero_stamp();}
  };
  Empty true_struct;
  Empty* false_val = nullptr;
  Empty* true_val = &true_struct;
  
  // Simulates a boolean with a pointer to an "Empty" dummy struct.
  // If false, then pointer is nullptr otherwise pointer to "true_struct".
  // Avoids level of indirection when storing false.
  struct atomic_bool {
    versioned_ptr<Empty> val;
    bool load() {return val.load() == true_val;}
    void validate() {val.validate();}
    void store(bool v) {val = (v ? true_val : false_val);}
    atomic_bool(bool v) : val(v ? true_val : false_val) {}
    bool operator=(bool b) {store(b); return b; }
  };

} // namespace tlf_internal

#endif // TLF_VERSIONED_POINTER_H_
