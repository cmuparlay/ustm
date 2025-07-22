args="-n 10000"
./test_ustm ${args}
./test_mvustm ${args}
./test_mvustm_longjmp ${args}
./test_mvustm_trythrow ${args}
./test_mvustm_unprotected ${args}
./test_2plsf ${args}
