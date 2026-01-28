#pragma once

template<typename K, int KEY_COL_COUNT, typename V, class DS_T>
class Multi_Col_Index_DS {
private:
    DS_T* ds;

public:
    Multi_Col_Index_DS() {
        ds = new DS_T();            
    }

    ~Multi_Col_Index_DS() {
        delete ds;
    }

    	
	void update(K key[KEY_COL_COUNT], const V& val) {
		ds->update(key, val);
	}
    V addKey(K key[KEY_COL_COUNT], const V& val) {
        return ds->addKey(key, val);
    }
    V removeKey(K key[KEY_COL_COUNT]) {
        return ds->removeKey(key);
    }
    V get(K key[KEY_COL_COUNT]) {
        return ds->get(key);
    }
	void getSuccessorKey(K key[KEY_COL_COUNT], K successorKey[KEY_COL_COUNT], V* successorVal) {
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
