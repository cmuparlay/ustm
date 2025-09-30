# ustm
Micro Software Transactional Memory

## Known Issues
#### GCC's Transactional Memory
We have encountered a number of problems when using gcc's built in support for transactional memory (gcctm). A list of these issues are documented below:
- If `always_inline` appears somewhere in the code, it is likely that compilation will fail. This is probably due to a compiler bug. We have dealt with this issue by commenting the `always_inline` attribute in stdlib when required. Another option is to attempt to change the stdlib version, but this is not guaranteed to succeed.
- gcctm overloads common allocation functions (e.g., `malloc`, `free`, `calloc`, `new`, `delete`), but it does not overload `aligned new`. Since we require aligned allocation in practically all benchmarks, we depend heavily on `aligned new`. Since it is not available, we  implement our own aligned allocation function. Because other transactional managers may use more optimized aligned allocation strategies, gcctm might suffer some performance penalty in comparison.
-  One of the benchmarks, `list_gcctm`, segfaults when compiled with `-O3`, hence we compile it with `-O2` instead.