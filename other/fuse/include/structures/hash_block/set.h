#define HASH 1 // indicates that Set needs a hash function
#define UPSERT 1 // indicates that supports upsert

#include "unordered_map.h"

template <typename K,
	  typename V,
	  class Hash = std::hash<K>,
	  class KeyEqual = std::equal_to<K>>
using unordered_map = verlib::hash_block<K,V,Hash,KeyEqual>;
