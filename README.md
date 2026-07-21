# ustm
Micro Software Transactional Memory

A header only multiversioned software transactional memory library
that supports general types, and without any aborts in user code.
Described in:

> Zachary Kent, Guy Blelloch and Andre Costa.
> *uSTM: A Lightweight and Efficient STM Supporting General Types and Deferred Aborts*.
> ACM Symposium on Parallelism in Algorithms and Architecture (SPAA), 2026.

The code is designed to be concise and portable, so that it should be
easy for others to adapt.  The whole code is in the two files:

```
include/ustm.h     (the main code)
include/uepoch.h   (a small epoch based collector, included in ustm.h)
```

The interface is:
```
  r = ustm::transaction(func) // runs a func in transaction
  r = ustm::transaction(func, true) // read only mode
  r = ustm::load(x) // loads value from x
  ustm::store(x, v) // stores v into x
  r = ustm::New<T>(args ...) // new object of type T
  ustm::Delete(x) // deletes x
  ustm::fence(x) // fence x so can use outside of a transaction
```

To try out:

```
mkdir build
cd build
cmake ..
cd benchmark
make -j 16
./arttree_mvustm -n "100000,10000000" -u "5,50" -z "0,.99" -trans "1,16" -r 2
../../benchmark/run_experiment.py all
```

The second to last line is just an example of an experiment on an
ARTtree with ustm. The -n gives the sizes, the -u the update
percentages, -z the zipfian distributions, -trans the transaction
sizes, and -r the number of rounds.  The last line runs all the
experiments in the paper and logs results to ```timings/```.

The directory structure is, roughly

```
  benchmark/   - our benchmark code
  include/     - our uSTM and some support files
  other/       - other STMs
  plot/        - our plotting code
  structures/  - the data structures tested
  test/        - some simple testing code
  timings/     - output of benchmark scripts
```

Note that the file `include/ustm_swiss.h` is a version of ustm with
dozens of ifdefs in it to try out all sorts of configurations that are
tested in the experiments (different timestamps, multiversioned or
not, with longjump aborts, ...).  The file `include/ustm.h` is
generated automatically from `include/ustm_swiss.h` by setting the
default declarations and running it through the preprocessor (see
script `include/strip.sh`).    We advise just looking at `ustm.h`.
