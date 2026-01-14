#define Range_Search 1
#define UPSERT 1 // indicates that supports upsert

#include "ordered_map.h"

template <typename K,
          typename V,
          typename Compare = std::less<K>>
using ordered_map = verlib::skiplist<K,V,Compare>;
