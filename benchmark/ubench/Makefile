CC=g++

TMS := GCC_TM TWOPLSF OneFile USTM
INCLUDES=-I../../other/ -I../../include
FLAGS=-O2 -std=c++17 -pthread -litm -g

all: libitm.h
#echo for some feedback, even though it's hardcoded
	@for tm in $(TMS); do \
		echo "${CC} ${INCLUDES} small_bench.cpp -o small_bench_$$tm ${FLAGS} -D$$tm"; \
		${CC} ${INCLUDES} small_bench.cpp -o small_bench_$$tm ${FLAGS} -D$$tm; \
	done
	

#need libitm.h, this should be a reasonably stable hyperlink to it
libitm.h:
	wget https://raw.githubusercontent.com/gcc-mirror/gcc/refs/heads/master/libitm/libitm.h

.PHONY: clean
clean:
	rm -f small_bench_* libitm.h
