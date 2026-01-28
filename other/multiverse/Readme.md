This repository contains the code used to produce the experiments in the paper Multiverse: Transactional Memory with Dynamic Multiversioning [PPoPP 2026] 

# Multiverse Overview

Mutliverse is a full feature multiversion software transactional memory (STM) that guarantees opacity and weak progressiveness. 

Mutliverse was designed with the goal of matching the performance of the state of the art unversioned STM, Deferred Clock Transactional Locking Locking (DCTL) [PPoPP 2024], while also supporting long-running reads in the presence of frequent updates.

Transactions in Multiverse are either versioned or unversioned. Unversioned transactions behave similarly to DCTL while versioned transcations utilize a snapshot style approach similar to Verlib [PPoPP 2024]. Multiple versioned and unversioned transactions can execute concurrently.

Multiverse dynamically versions individual addresses depending on the needs of the workload. 

Multiverse operates in one of 4 global modes: 2 stable modes and 2 transient modes. Individual transactions have a local mode which they acquire by reading the global mode.

Multiverse relies on a dedicated background thread for the purpose of unversioning and managing the global mode.


# CLONING this repo requires a special command to ensure all submodules are cloned.

Cloning this repo is complicated by the existence of *submodules*:

```
git clone https://gitlab.com/Coccimiglio/mvcc_tm.git --recurse-submodules
```

# Compiling
To compile all of the different data structures and TMs simply run the provided compile.sh script:

```
./compile.sh
```

If you want to compile a specific TM or data structure you can edit the Makefile or provide Makefile arguments for NO_LIB_TM_ALGS and partial_algorithm_names. 
If you want to only compile DCTL or Multiverse there are specific compile scripts included.


# General Usage

If you wish to use Multiverse in your own codebase simply include the multiverse/multiverse.hpp header.
NOTE: If you include the header from multiple compilation units you will need to move the global variable definitions at the bottom of the multiverse/multiverse.hpp file into a seperate .cpp file.

# Tunable Parameters

Multiverse uses various tunable parameters all of which are defined at the top of the multiverse/multiverse.hpp file as indicated by the Tunable parameters comment block. We have selected some reasonable defaults however you might want to change these paremeters depending on your use case.

# Acknowledgements

Thanks to Pedro Ramalhete for some suggested optimizations to the mapping function and memory layout of some internal Multiverse structs 