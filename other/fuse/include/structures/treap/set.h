#include "ordered_map.h"
#define UPSERT 1 // indicates that supports upsert

template <typename K,
	  typename V,
	  class Hash = parlay::hash<K>,
	  typename Compare = std::less<K>>
using ordered_map = verlib::treap<K,V,Hash,Compare>;
