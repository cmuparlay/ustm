#include <stdio.h>
#include <cstring>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>


#include "../../plaf.h"
#include "../../binding.h"
#include "../../papi/papi_util_impl.h"
#include "../nvram_file_mapping.h"

#define PADDING_BYTES 128
// #define DISABLE_THEADS

#define PMEM_POOL_SIZE (8*1024*1024*1024ULL)
#define PMEM_FILE_PATH "/mnt/pmem1_mount/wbinvdNumaTest"

__thread int tid = 0;

volatile bool done = false;
volatile bool start = false;
// volatile bool flushOnce = false;
volatile bool flushOnceSocket1 = false;
volatile bool flushOnceSocket2 = false;

struct paddedEntry {	
	// PAD;
	volatile uint64_t num;
	PAD;
};

paddedEntry* data;

void doWork(int tid) {
	data[tid + 50].num += 1;
}

// void thread_timed(int __tid, paddedEntry* data, int fd){
void thread_timed(int __tid, int fd){
	tid = __tid;
	binding_bindThread(tid);
	papi_create_eventset(tid);
	__sync_synchronize();	
	while (!start) { SOFTWARE_BARRIER; }
	papi_start_counters(tid);

	

	while(!done) {
		// printf("%d\n", tid);
		// data[tid].num += tid;
		// data[tid + 25].num += 1;
		doWork(tid);

		// if (tid == 0 && flushOnce){
		if (tid == 0 && flushOnceSocket1 || tid == 48 && flushOnceSocket2){
			char *buf;
#ifndef DISABLE_WBINVD			
			int ret = write(fd, buf, 0);
			if (ret < 0){
				perror("Failed to flush.");			
			}
			else {
				printf("WBINVD Successful\n");
			}
#endif
			if (tid == 0){
				flushOnceSocket1 = false;
			}
			else {
				flushOnceSocket2 = false;
			}

			// flushOnce = false;
		}
	}

	SOFTWARE_BARRIER; 
    papi_stop_counters(tid); 
    SOFTWARE_BARRIER;
}

int main(int argc, char** argv){

	printf("Size of padded entry %ld\n", sizeof(paddedEntry));

	int numThreads = 1;
	int milisToRun = 1000;	

	if (argc < 2) {
		printf("Error: Must provide -t seconds to run and -n number of threads\n.");
		printf("Exiting.\n");
		return -1;
	}

	for (int i=1;i<argc;++i) {
		if (strcmp(argv[i], "-t") == 0) {
        	milisToRun = atoi(argv[++i]);
			printf("Time to run = %d\n", milisToRun);
        } else if (strcmp(argv[i], "-n") == 0) {
        	numThreads = atoi(argv[++i]);
			printf("Num threads = %d\n", numThreads);
        } else if (strcmp(argv[i], "-pin") == 0) { // e.g., "-pin 1.2.3.8-11.4-7.0"
			binding_parseCustom(argv[++i]); // e.g., "1.2.3.8-11.4-7.0"
			std::cout<<"parsed custom binding: "<<argv[i]<<std::endl;
		}
	}
		
	void* baseAddr = (void*)0x7fddc0000000;
	size_t poolSize = PMEM_POOL_SIZE;
	printf("Pool size = %ld\n", poolSize);
	int pmemFd;
	void* mapStartAddr = initializePersistentMemoryFileMapping(pmemFd, PMEM_FILE_PATH, poolSize, true, baseAddr);
	printf("Pmem base address: %p\n", mapStartAddr);

	// paddedEntry* data = new paddedEntry[numThreads * 50];
	// paddedEntry* data = new (mapStartAddr) paddedEntry[numThreads * 50];
	data = new (mapStartAddr) paddedEntry[numThreads * 100];
	
	for (int i=0;i<numThreads * 50;++i) {
		data[i].num = 0;
	}

	binding_configurePolicy(numThreads);

    // print actual thread pinning/binding layout
    std::cout<<"ACTUAL_THREAD_BINDINGS=";
    for (int i=0;i<numThreads;++i) {
        std::cout<<(i?",":"")<<binding_getActualBinding(i);
    }
    std::cout<<std::endl;
    if (!binding_isInjectiveMapping(numThreads)) {
        std::cout<<"ERROR: thread binding maps more than one thread to a single logical processor"<<std::endl;
        exit(-1);
    }

	int fd;
	// char *buf;
	
	fd = open("/dev/global_flush", O_RDWR);
	if (fd < 0){
		perror("Failed to connect to device\n");
		return errno;
	}

	papi_init_program(numThreads);

	std::thread * threads[MAX_THREADS_POW2];
    for (int i=0;i<numThreads;++i) {
#ifndef DISABLE_THEADS
        // threads[i] = new std::thread(thread_timed, i, data, fd);        
		threads[i] = new std::thread(thread_timed, i, fd);        
#endif
    }

 	

	start = true;
	__sync_synchronize();


	timespec tsExpected;
    tsExpected.tv_sec = milisToRun / 1000 / 2;
    tsExpected.tv_nsec = (milisToRun % 1000) * ((__syscall_slong_t) 1000000) / 2;


	nanosleep(&tsExpected, NULL);
	SOFTWARE_BARRIER;
	// flushOnce = true;
	flushOnceSocket1 = true;
	flushOnceSocket2 = true;

	tsExpected.tv_sec = milisToRun / 1000 / 2;
    tsExpected.tv_nsec = (milisToRun % 1000) * ((__syscall_slong_t) 1000000) / 2;
	nanosleep(&tsExpected, NULL);
	SOFTWARE_BARRIER;

	done = true;
	__sync_synchronize();
	for (int i=0;i<numThreads;++i) {
#ifndef DISABLE_THEADS
		threads[i]->join();
#endif
    }

	papi_deinit_program();

	
	


	

	close(fd);

	

	long long totalAll = 0;
	for (int i=0;i<numThreads;++i) {
		totalAll += data[tid].num;
		// printf("%ld\n", data[tid].num);
	}

#ifndef USE_PAPI
	printf("NOTE: Papi is off\n");
#else
	printf("Papi Counters:\n");
	papi_print_counters();
#endif

	

	return 0;
}