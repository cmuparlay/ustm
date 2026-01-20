#include "ordered_map.h"
#define UPSERT 1 // indicates that supports upsert

template <typename K,
          typename V,
          typename Compare = std::less<K>>
using ordered_map = verlib::avltree<K,V,Compare>;
