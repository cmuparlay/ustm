CC=g++-13

TMS := GCC_TM GCC_TM_ANN TWOPLSF OneFile USTM
INCLUDES = -I../../other/ -I../../include
FLAGS = -O2 -std=c++17 -pthread -litm -g

all: libitm.h
#echo for some feedback, even though it's hardcoded
	@for tm in $(TMS); do \
		TMFLAGS=""; \
		if [ "$$tm" = "GCC_TM_ANN" ]; then TMFLAGS="-fgnu-tm"; fi; \
		echo "$(CC) $(INCLUDES) small_bench.cpp -o small_bench_$$tm $(FLAGS) $$TMFLAGS -D$$tm"; \
		$(CC) $(INCLUDES) small_bench.cpp -o small_bench_$$tm $(FLAGS) $$TMFLAGS -D$$tm; \
	done

#need libitm.h, this should be a reasonably stable hyperlink to it
libitm.h:
	wget https://raw.githubusercontent.com/gcc-mirror/gcc/refs/heads/master/libitm/libitm.h

.PHONY: clean
clean:
	rm -f small_bench_* libitm.h
