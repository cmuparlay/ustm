#pragma once

extern double INS_FRAC;
extern double DEL_FRAC;

template<typename K, template <typename> class TMTYPE>
class ArrayTest {

private:
    TMTYPE<TMTYPE<long>*> arr;      
    uint64_t size;    
    uint64_t maxAccessArraySize;
    Random64* rngs;

public:
    ArrayTest(Random64* _rngs, uint64_t _size) {
		size = _size;
        //this might be too much
        maxAccessArraySize = size / 2;
        rngs = _rngs;
		arr = (TMTYPE<long>*)trinityvrtl2::Trinity::pmalloc(size * sizeof(TMTYPE<long>));
		for (int i = 0; i < size; i++) {
			arr[i] = 0;
		}
    }


    ~ArrayTest() {
    }


    bool insert(int tid) {       
        long accessArraySize = 2+rngs[tid].next(maxAccessArraySize);
        // printf("access size %ld\n", accessArraySize);
        long total = 0;
        trinityvrtl2::Trinity::updateTx<bool>([&] () {
        	for (int i = 0; i < accessArraySize; i++) {
                double op = rngs[tid].next(100000000) / 1000000.;
                uint64_t slot = rngs[tid].next(accessArraySize);
                if (op < INS_FRAC) {
                    arr[slot]++;
                } else if (op < INS_FRAC+DEL_FRAC) {
                    arr[slot]--;
                } else {
                    total += arr[slot];
                }
            }
            return true;
        });
        if (total < 0) {
			return false;
		}
		else {
			return true;
		}
    }

    bool remove(int tid) {  
        long accessArraySize = 2+rngs[tid].next(maxAccessArraySize);
        // printf("access size %ld\n", accessArraySize);
        long total = 0;        
        trinityvrtl2::Trinity::updateTx<bool>([&] () {
        	for (int i = 0; i < accessArraySize; i++) {
                double op = rngs[tid].next(100000000) / 1000000.;
                uint64_t slot = rngs[tid].next(accessArraySize);
                if (op < INS_FRAC) {
                    arr[slot]++;
                } else if (op < INS_FRAC+DEL_FRAC) {
                    arr[slot]--;
                } else {
                    total += arr[slot];
                }
            }
            return true;
        });
        if (total < 0) {
			return false;
		}
		else {
			return true;
		}
    }

    bool contains(int tid) { 
		// long accessArraySize = 2+rngs[tid].next(maxAccessArraySize);
        // // printf("access size %ld\n", accessArraySize);
        // long total = 0;
        // trinityvrtl2::Trinity::updateTx<bool>([&] () {
        // 	for (int i = 0; i < accessArraySize; i++) {
        //         double op = rngs[tid].next(100000000) / 1000000.;
        //         uint64_t slot = rngs[tid].next(accessArraySize);
        //         total += arr[slot];
        //     }
        //     return true;
        // });
        // if (total < 0) {
		// 	return false;
		// }
		// else {
		// 	return true;
		// }  
        long accessArraySize = 2+rngs[tid].next(maxAccessArraySize);
        // printf("access size %ld\n", accessArraySize);
        long total = 0;        
        trinityvrtl2::Trinity::updateTx<bool>([&] () {
        	for (int i = 0; i < accessArraySize; i++) {
                double op = rngs[tid].next(100000000) / 1000000.;
                uint64_t slot = rngs[tid].next(accessArraySize);
                if (op < INS_FRAC) {
                    arr[slot]++;
                } else if (op < INS_FRAC+DEL_FRAC) {
                    arr[slot]--;
                } else {
                    total += arr[slot];
                }
            }
            return true;
        });
        if (total < 0) {
			return false;
		}
		else {
			return true;
		}    
    }
};
