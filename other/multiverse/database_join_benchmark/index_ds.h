#pragma once

template<typename K, typename V, class DS_T>
class Index_DS {
private:
    DS_T* ds;

public:
    Index_DS() {
        ds = new DS_T();            
    }

    ~Index_DS() {
#ifdef ENABLE_DS_CLEANUP
        delete ds;
#endif
    }

    void deinitThread() {
        ds->deinitThread();
    }
    	
	void update(const K& key, const V& val) {
		ds->update(key, val);
	}
    V addKey(const K& key, const V& val) {
        return ds->addKey(key, val);
    }
    V removeKey(const K& key) {
        return ds->removeKey(key);
    }
    V get(const K& key) {
        return ds->get(key);
    }
    // bool contains(const K& key) {
    //     return ds->contains(key);
    // }

	void getSuccessorKey(const K& key, K* successorKey, V* successorVal) {
		ds->getSuccessorKey(key, successorKey, successorVal);
	}

    int rangeQuery(const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        return ds->rangeQuery(lo, hi, resultKeys, resultValues);
    }

    void printSummary() {
		// return ds->printSummary();        
    }
    bool validate() {
        return ds->validate();        
    }
};
