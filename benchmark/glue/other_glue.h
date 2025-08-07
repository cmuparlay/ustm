#pragma once

#ifdef TL2O
#include "TL2Orig.hpp"
namespace ext_stm = tl2orig;
namespace stm { struct versioned {}; }
#define LimitedSTM

#elif defined(Tiny)
#include "TinySTM.hpp"
namespace ext_stm = tinystm;
namespace stm { struct versioned {}; }
#define LimitedSTM

#elif defined(OneFile)
#include "OneFileWF.hpp"
namespace ext_stm = ofwf;
namespace stm { using versioned = ofwf::tmbase; }

#elif defined(TWOPLSF)
#include "2PLSF.hpp"
namespace stm { struct versioned {}; }
namespace ext_stm = twoplsf;
#else
No STM Specified
#endif

namespace stm {

#ifdef OneFile
  template <typename F>
  auto atomic_region(F &&func) {
    using T = decltype(func());
    T result;
    ext_stm::updateTx<bool>([&] {result = func(); return true;});
    return result;
  }

  template <typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    using T = decltype(func());
    T result;
    ext_stm::readTx<bool>([&] {result = func(); return true;});
    return result;
  }
#else
  template <typename F>
  auto atomic_region(F &&func) {
    return ext_stm::updateTx<decltype(func())>(func);  }

  template <typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    return ext_stm::readTx<decltype(func())>(func);  }
#endif
  
  template <typename F>
  auto atomic_read_only_singleton(F f) {
      return atomic_region(f);
  }

  template <typename T>
  void Delete(T *ptr) { ext_stm::tmDelete(ptr); }
      
  template <typename T, typename... Args>
  T *New(Args... args) { return ext_stm::tmNew<T>(args...); }

  template <typename T>
  struct atomic {
  private:
    ext_stm::tmtype<T> val;
  public:
    atomic(T v) : val(v) {}
    atomic() {}
    void store(T v) { val = v; }
    T load() const { return val.pload(); }
    T load_unprotected() const { return val.pload();}
    void validate() const { }
    void init(T x) { store(x); }
    T operator=(T b) { store(b); return b; }
  };

  std::pair<long,long> statistics() {
#if defined(STMStats) && defined(TWOPLSF)
    auto& stm = twoplsf::gSTM;
    uint64_t totalAborts = 0;
    uint64_t totalCommits = 0;
    for (int i=0; i < twoplsf::REGISTRY_MAX_THREADS; i++) {
      auto& d = stm.opDesc[i];
      totalAborts += d.numAborts;
      totalCommits += d.numCommits;
      d.numAborts = d.numCommits = 0;
    }
    return std::pair(totalAborts, totalCommits);
#else
    return std::pair(0ul,0ul);
#endif
  }
}
