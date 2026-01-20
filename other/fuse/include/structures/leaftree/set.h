#include "ordered_map.h"

template <typename K,
          typename V,
          typename Compare = std::less<K>>
using ordered_map = verlib::leaftree<K,V,Compare>;
