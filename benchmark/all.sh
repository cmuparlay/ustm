./hash_block_mvustm -n 100000,10000000 -u 0,5,50 -z 0,.99 -r 1 -trans 16
./btree_mvustm -n 100000,10000000 -u 0,5,50 -z 0,.99 -r 1 -trans 16
./arttree_mvustm -n 100000,10000000 -u 0,5,50 -z 0,.99 -r 1 -trans 16
./leaftree_mvustm -n 100000,10000000 -u 0,5,50 -z 0,.99 -r 1 -trans 16
./skiplist_mvustm -n 100000,10000000 -u 0,5,50 -z 0,.99 -r 1 -trans 16
./treap_mvustm -n 100000,10000000 -u 0,5,50 -z 0,.99 -r 1 -trans 16
./list_mvustm -n 100,1000 -u 0,5,50 -z 0,.99 -r 1 -trans 16
./btree_mvustm -n 1000000 -u 100 -z 0 -trans 16 -rs 1,10,100,1000,10000,100000,1000000 -rqp 50
./arttree_mvustm -n 1000000 -u 100 -z 0 -trans 16 -rs 1,10,100,1000,10000,100000,1000000 -rqp 50 
