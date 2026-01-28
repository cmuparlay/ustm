#pragma once

#include <map>
#include <string>
#include "index_ds.h"
#include "multi_column_index_ds.h"

#include "common/global_type_defines.h"
#include "data.h"
#include "ds_tm/TMHashMapFixedSize.hpp"
#include "ds_tm/TMHashMapFixedSizeMultiColumnKey.hpp"
#include "ds_tm/TMAbtree_2_Col_Key.hpp"
#include "ds_tm/TMAbTree.hpp"

#include <functional>
#define SINLGE_COLUMN_KEY_CMP std::less<SINGLE_COLUMN_KEY_T>

#define TWO_COLUMN_KEY_CMP std::less<TWO_COLUMN_KEY_T>

#define DS_SINGLE_COLUMN_TEMPLATE_SPECIALIZATION 

#define HASHMAP_SINGLE_COL_KEY TMHashMapFixedSize<SINGLE_COLUMN_KEY_T,VAL_T,SINLGE_COLUMN_KEY_CMP>
#define ABTREE_SINGLE_COL_KEY ab_tree_single_column_key::ABTreeTM<SINGLE_COLUMN_KEY_T,VAL_T>

#define DS_WITH_RQ ABTREE_SINGLE_COL_KEY
#define DS_NO_RQ ABTREE_SINGLE_COL_KEY


class IndexManager {
private:

public:
	//item table index
	Index_DS<SINGLE_COLUMN_KEY_T, VAL_T, DS_NO_RQ> itemTable_itemId_index;	
	Index_DS<SINGLE_COLUMN_KEY_T, VAL_T, DS_NO_RQ> itemTable_categoryId_index;				
			
	//order table indexes
	Index_DS<SINGLE_COLUMN_KEY_T, VAL_T, DS_NO_RQ> orderTable_orderId_index;	
	Index_DS<SINGLE_COLUMN_KEY_T, VAL_T, DS_WITH_RQ> orderTable_date_index;			
	Index_DS<TwoColumnKey, VAL_T, TMAbtree_2_Col_Key> orderTable_customerId_date_index;

	void printSummary() {		
	}

	void deinitThread() {
		itemTable_itemId_index.deinitThread();
		itemTable_categoryId_index.deinitThread();
		orderTable_orderId_index.deinitThread();
		orderTable_date_index.deinitThread();
		orderTable_customerId_date_index.deinitThread();
	}

	bool validate() {
		printf("Warning IndexManager validation disabled\n");
		bool valid = true;
		return valid;
	}
};