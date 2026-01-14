#define Range_Search 1
#define RADIX 1
#define UPSERT 1 // indicates that supports upsert

#include "ordered_map.h"

template <typename K,
          typename V,
          typename String = verlib::int_string<K>>
using ordered_map = verlib::arttree<K,V,String>;
