#ifndef BUFFERED_DURABLE_TL2_DCSS_SHARDED_COUNTER
#define BUFFERED_DURABLE_TL2_DCSS_SHARDED_COUNTER

#include <stdint.h>
#include "../../setbench/common/dcss/dcss_impl.h"

#ifndef FAA
    #define FAA __sync_add_and_fetch 
#endif

#ifndef BCAS
    #define BCAS __sync_bool_compare_and_swap
#endif

//This is really made specifically for the buffered durable TM the naming might be a bit
//  too generic for the specifics of the implementation
class DCSS_DualCounter_Approximate {
private:
    dcssProvider<void*>* prov;
    const int ERROR_CONST_SIZE;
    const int ERROR_CONST_CTSLF;    
    int threadCount;    
        
    struct padded_int{
        uint64_t v;
        PAD;
    };

    padded_int *local_counters;


public:
    volatile uint64_t global_count = 0;

    DCSS_DualCounter_Approximate(int _numThreads, int size_error, int ctslf_error, dcssProvider<void*>* _prov) : ERROR_CONST_SIZE(size_error), ERROR_CONST_CTSLF(ctslf_error) {
        prov = _prov;
        threadCount = _numThreads;
        local_counters = new padded_int[_numThreads];
        
        for (int i = 0; i < _numThreads; i++){
            local_counters[i].v = 0;
        }
    }
    
    ~DCSS_DualCounter_Approximate(){
        delete[] local_counters;
    }
    
    int inc(int tid, casword_t* otherAddr, casword_t otherExp) {                        
        //increment local counter
		return add(tid, 1, otherAddr, otherExp);        
    }

	int add(int tid, uint32_t wrSetSize, casword_t* otherAddr, casword_t otherExp) {             
        uint32_t size = (uint32_t)(local_counters[tid].v);
        uint32_t* ctslfPtr = (uint32_t*)(&local_counters[tid].v);        
        ctslfPtr++;
        uint32_t ctslf = *(ctslfPtr);
        
        ctslf++;
        size += wrSetSize;

        uint64_t newVal = (((uint64_t)(ctslf)) << 32) | (size);;
        uint64_t tmp = prov->readVal(tid, &local_counters[tid].v);

        int result = prov->dcssVal(tid, otherAddr, otherExp, 
            (casword_t*)&local_counters[tid].v, (casword_t)tmp, newVal).status;
        
        if (result == DCSS_SUCCESS) {            
            //after c * n increments
            if (size >= ERROR_CONST_SIZE || ctslf >= ERROR_CONST_CTSLF){
                // uint64_t oldCount = FAA(&global_count, newVal);                
                
                while (true) {
                    uint64_t oldCount = prov->readVal(tid, &global_count);
                    uint64_t newCount = oldCount + newVal;
                    int result2 = prov->dcssVal(tid, otherAddr, otherExp, 
                        (casword_t*)&global_count, (casword_t)oldCount, newCount).status;

                    if (result2 == DCSS_SUCCESS) {
#ifdef DEBUG_GUY
                        printf("Tid=%d - Old Global Count = %ld - New Global Count = %ld\n", tid, oldCount, newCount);
#endif
                        break;
                    }         

                    if (result2 == DCSS_FAILED_ADDR1) {
                        return result2;
                    }      
                }                

                //reset local to 0
                // local_counters[tid].v = 0;
                while (true) {
                    tmp = prov->readVal(tid, &local_counters[tid].v);
                    if (tmp == 0) {
                        break;
                    }
                    int result3 = prov->dcssVal(tid, otherAddr, otherExp, 
                        (casword_t*)&local_counters[tid].v, (casword_t)tmp, 0).status;
                    if (result3 != DCSS_FAILED_ADDR2) {
                        break;
                    }
                }
            } 
        }

        if (result == DCSS_FAILED_ADDR2) {
            assert(false);
            printf("Error\n");
        }

        return result;
    }
    
    uint64_t read(int tid) {
        //we don't have an exact read for this approximate counter so just return global count
        //error is bounded by c * threadCount^2 
		//we dont need an exact read because we will 'lock' all shards when we care about 
		//the exact size
        return prov->readVal(tid, &global_count);
    }

    int reset(int tid, casword_t* otherAddr, casword_t otherExp) {
        int result = -1;
        for (int i = 0; i < threadCount; i++){            
            while (true) {
                casword_t expLocalCount = prov->readVal(tid, &local_counters[i].v);
                result = prov->dcssVal(tid, otherAddr, otherExp, 
                    (casword_t*)&local_counters[i].v, expLocalCount, 0).status; 

                if (result == DCSS_SUCCESS) {
                    break;
                }

                if (result == DCSS_FAILED_ADDR1) {
                    return result;
                }
            }            
        }

        result = -1;

        while (true) {
            casword_t expCount = prov->readVal(tid, &global_count);
            if (expCount == 0) {
                break;
            }

            int result = prov->dcssVal(tid, otherAddr, otherExp, 
                (casword_t*)&global_count, expCount, 0).status;            

            if (result == DCSS_SUCCESS || result == DCSS_FAILED_ADDR1) {
                break;
            }            
        }

        return result;
    }
};

class DCSSCounterApproximate {
private:
    dcssProvider<void*>* prov;
    const int ERROR_CONST = 1000;
    int threadCount;
    int threshold;
        
    struct padded_int{
        uint64_t v;
        PAD;
    };

    padded_int *local_counters;
public:
    volatile uint64_t global_count = 0;

    DCSSCounterApproximate(int _numThreads, dcssProvider<void*>* _prov) {
        prov = _prov;
        threadCount = _numThreads;
        local_counters = new padded_int[_numThreads];
        
        for (int i = 0; i < _numThreads; i++){
            local_counters[i].v = 0;
        }

        threshold = ERROR_CONST * threadCount;
    }
    
    ~DCSSCounterApproximate(){
        delete[] local_counters;
    }
    
    int inc(int tid, casword_t* otherAddr, casword_t otherExp) {                        
        //increment local counter
		return add(tid, 1, otherAddr, otherExp);        
    }

	int add(int tid, uint64_t num, casword_t* otherAddr, casword_t otherExp) {                        
        //increment local counter
        // uint64_t tmp = local_counters[tid].v + num;
        uint64_t tmp = local_counters[tid].v;
        int result = prov->dcssPtr(tid, otherAddr, otherExp, 
            (casword_t*)&local_counters[tid], (casword_t)tmp, tmp + num).status;
        
        if (result == DCSS_SUCCESS) {            
            //after c * n increments
            if (tmp >= threshold){
                FAA(&global_count, tmp + num);
                            
                //reset local to 0
                local_counters[tid].v = 0;
            } 
        }

        return result;
    }
    
    int64_t read() {
        //we don't have an exact read for this approximate counter so just return global count
        //error is bounded by c * threadCount^2 
		//we dont need an exact read because we will 'lock' all shards when we care about 
		//the exact size
        return global_count;
    }
};


class CounterApproximate {
private:
    const int ERROR_CONST = 1000;
    int threadCount;
    int threshold;
        
    struct padded_int{
        uint64_t v;
        PAD;
    };

    padded_int *local_counters;
public:
    volatile uint64_t global_count = 0;

    CounterApproximate(int _numThreads) {    
        threadCount = _numThreads;
        local_counters = new padded_int[_numThreads];
        
        for (int i = 0; i < _numThreads; i++){
            local_counters[i].v = 0;
        }
        
        threshold = ERROR_CONST * threadCount;
    }
    
    ~CounterApproximate(){
        delete[] local_counters;
    }
    
    int inc(int tid) {                        
        //increment local counter
		return add(tid, 1);        
    }

	int add(int tid, uint64_t num) {                        
        //increment local counter
        uint64_t tmp = local_counters[tid].v + num;
        
        if (tmp == threshold){
            FAA(&global_count, tmp + num);
                        
            //reset local to 0
            local_counters[tid].v = 0;
        } 
        
        return tmp;
    }
    
    int64_t read() {
        //we don't have an exact read for this approximate counter so just return global count
        //error is bounded by c * threadCount^2 
		//we dont need an exact read because we will 'lock' all shards when we care about 
		//the exact size
        return global_count;
    }
};


#endif
