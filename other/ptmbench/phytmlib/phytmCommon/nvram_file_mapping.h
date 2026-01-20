#pragma once

/*
 * This is originally from https://github.com/pramalhe/RedoDB/tree/master/ptms
 * 		original authors: 
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 * 
 * Most of this is just adapated from the original with the CXPUC specific stuff removed
 * This was added to setbench when we wanted to test some stuff related to the CXPUC
*/

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <fcntl.h>
#include <cstdint>
#include <sys/stat.h>

#ifndef MMAP_FLAGS
	#if defined(MAP_SHARED_VALIDATE) && defined(MAP_SYNC) && !defined(DISABLE_PMEM)
		#define MMAP_FLAGS MAP_SHARED_VALIDATE | MAP_SYNC
	#else 
		#define MMAP_FLAGS MAP_SHARED
	#endif
#endif

void* initializePersistentMemoryFileMapping(int& fd, const char* filePath, size_t maxSize, bool checkIfExists = true, void* startAddr = nullptr);
void createFile(int& fd, const char* filePath, size_t maxSize);
void recover(int& fd, const char* filePath, size_t maxSize, void* startAddr);	
inline void* mmapFile(int& fd, size_t maxSize, void* startAddr = nullptr, int flags = MMAP_FLAGS);

void* initializePersistentMemoryFileMapping(int& fd, const char* filePath, size_t maxSize, bool checkIfExists, void* startAddr){		
	printf("PMEM file full path: %s\n", filePath);
	printf("PMEM pool size = %ld\n", maxSize);
	
	// Check if the file already exists or not
	struct stat buf;
	if (stat(filePath, &buf) == 0) {				
		//We do not want to actually recover anything in the tests because
		// we want to start fresh each time so this function just removes the old pmem file for now		
		recover(fd, filePath, maxSize, startAddr);
	} else {
		createFile(fd, filePath, maxSize);
	}

	void* gotAddr = mmapFile(fd, maxSize, startAddr);
	return gotAddr;
}

void recover(int& fd, const char* filePath, size_t maxSize, void* startAddr){	
	// GUY:: File exists if we wanted to recover we would open it now
	// fd = open(filePath, O_RDWR|O_CREAT, 0755);
	// assert(fd >= 0);
	
	printf("The PMEM file already exits. Removing old file and recreating.\n");
	remove(filePath);
	
	createFile(fd, filePath, maxSize);
}

void createFile(int& fd, const char* filePath, size_t maxSize){
	fd = open(filePath, O_RDWR|O_CREAT, 0755);
	assert(fd >= 0);
	if (lseek(fd, maxSize-1, SEEK_SET) == -1) {
		perror("lseek() error");
	}
	if (write(fd, "", 1) == -1) {
		perror("write() error");
	}
}

void* mmapFile(int& fd, size_t maxSize, void* startAddr, int flags){
	void* gotAddr = mmap(startAddr, maxSize, (PROT_READ | PROT_WRITE), flags, fd, 0);

	if (gotAddr == MAP_FAILED) {
		printf("ERROR: mmap() is not working ...\n");
		perror("mmap error");
		printf("Flags = %d\n", flags);
		printf("MAP_SHARED_VALIDATE | MAP_SYNC = %d\n", MAP_SHARED_VALIDATE | MAP_SYNC);
		printf("MAP_SHARED = %d\n", MAP_SHARED);
		assert(false);
		exit(-1);
	}
	if (startAddr && gotAddr != startAddr) {
		printf("NOTE: mmap() got different address than expected...\n");
		printf("gotAddr = %p instead of %p\n", gotAddr, startAddr);
	}

	return gotAddr;
}