# Verlib data structures

Structures in each `set.h` and their included `ordered_map.h` or
`unordered_map.h` files need to support the following interface to be
used with our benchmarking code.  The `print`, `stats`, `shuffle` and
`clear` functions can be empty.

### Ordered maps

For ordered maps we have:

```
template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct ordered_map {
  type K = K_;
  type V = V_;

  // n can be used as the predicted ultimate size
  // most structures ignore it
  ordered_map(long n = 0);

  // find to be used in isolation
  std::optional<V> find(const K& k);

  // find to be used in transaction (there is a read lock at
  // the destination)
  std::optional<V> find_locked(const K& k);

  // find without smr.  Not safe to use concurrently unless
  // the user wraps protection.
  std::optional<V> find_(const K& k);

  // Insert but do not overwrite if already there
  // Return true if not there and inserted
  bool insert(const K& k , const V& v);

  // Remove entry and return true if it was in the map.
  bool remove(const K& k);

  long check(); // check consistency and return size
  void print(); // print contents
  static void clear(); // clear e.g. epoch collector
  static void stats(); // memory usage stats
  static void shuffle(long n); // deprecated
};
```

### Unordered maps

Unordered maps need to start with:

```
template <typename K_,
	  typename V_,
	  class Hash = std::hash<K_>,
	  class KeyEqual = std::equal_to<K_>>
struct unordered_map {
```

and include `#define HASH`.  Otherwise the interface is the same.

### Radix maps

Radix maps need to start with:

```
template <typename K,
          typename V,
          typename String = verlib::int_string<K>>
struct ordered_map {
```

and include `#define RADIX`.  Otherwise the interface is the same.
The `String` structure must support:

```
template <typename K>
struct string {
  // size of the key in bytes
  static int length(const K& key); 
  // the i-th most significant byte (0 being the most significant)
  static int get_byte(const K& key, int pos); 
};
```

### Range Searching

If a data structure includes `#define Range_Search` then
it needs to include:

```
  template<typename AddF>
  void range_(AddF& add_function, const K& start, const K& end);
```

The `add_function` must have type `void f(const K& k, const V& v)`.
It is applied to each key-value pair in the range.   The range
is inclusive of both `start` and `end`.

### Upsert

If a data structure includes `#define UPSERT` then
it needs to include:

```
  void upsert(const K& k, const V& v);
```

Unlike `insert`, which does nothing if the key is in the map, it needs
to overwrite an entry if it is already in the map.
