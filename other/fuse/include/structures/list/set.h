#define Range_Search 1
#include "ordered_map.h"

template <typename K,
          typename V,
          typename Compare = std::less<K>>
using ordered_map = verlib::list<K,V,Compare>;
