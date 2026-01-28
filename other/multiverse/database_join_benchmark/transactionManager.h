#pragma once

#include "common/global_type_defines.h"
#include "indexManager.h"

// #define MAX_CUSTOMERS 10000000
#define MAX_ITEM_PRICE 1000
// #define MAX_ORDER_ITEM_COUNT 100

#define UPDATE_ITEM_PRICE_DELTA 10

// #define MAX_DATE 500

#ifndef MAX_KEYS_PER_NODE
#define MAX_KEYS_PER_NODE 32
#endif

class TransactionManager {
public:
	IndexManager indexManager;

	void init() {
	}

	void printAllIndexSummaries() {	
		indexManager.printSummary();
	}

	bool validateAllIndexes() {
		return indexManager.validate();
	}

	void deinitThread() {
		indexManager.deinitThread();
	}

	//Txns:
	//---------- 

	// Add new item to the item table	
	bool txn_companyAddNewItem(const int tid, Random64* rng) {		
		bool retVal = false;

		while (!retVal) {
			uint64_t itemId = 1+rng->next(MAX_ITEM_ID);
        	uint64_t price = 1+rng->next(MAX_ITEM_PRICE);
	        uint64_t category = 1+rng->next(CATEGORY_COUNT);

			TM_T::template updateTx<bool>([&] () {
				Row* new_item_table_row = TM_T::tmNew<Row>();
				new_item_table_row->columns[ITEM_ID] = itemId;
				new_item_table_row->columns[ITEM_PRICE] = price;
				new_item_table_row->columns[ITEM_CATEGORY] = category;

				if (NO_VAL != indexManager.itemTable_itemId_index.addKey(itemId, (void*)new_item_table_row)) {
					return false;
				}
				
				//there is no need to have this be in the same transaction we could seperate it
				multiValueItemNode* top = (multiValueItemNode*)indexManager.itemTable_categoryId_index.get(category);	
				multiValueItemNode* newValueNode = TM_T::tmNew<multiValueItemNode>();
				newValueNode->data = (void*)itemId;
				newValueNode->next = top;			
				indexManager.itemTable_categoryId_index.update(category, newValueNode);
				retVal = true;
				return true;
			});
		}

		return retVal;
	}

	bool txn_customerAddNewOrder(const int tid, Random64* rng) {	
		bool retVal = false;
		uint64_t date;
		
		while (!retVal) {
			SINGLE_COLUMN_KEY_T orderId = 1+rng->next(MAX_ORDER_ID);		
			SINGLE_COLUMN_KEY_T customerId = 1+rng->next(MAX_CUSTOMER_ID);							
			date = 1+rng->next(MAX_DATE);
			SINGLE_COLUMN_KEY_T category = 1+rng->next(CATEGORY_COUNT);
			
			TM_T::template updateTx<bool>([&] () {
				//get random category			
				void* categoryItems;
				indexManager.itemTable_categoryId_index.getSuccessorKey(category, &category, &categoryItems);			

				// select random number of items from category
				multiValueItemNode* currItem = (multiValueItemNode*)categoryItems;
				int orderItemCount = 1+rng->next(MAX_ORDER_ITEM_COUNT);
				int currOrderItemCount = 0;
				multiValueItemNode* orderItems = nullptr;
				
				uint64_t orderValue = 0;
				while (currItem && currOrderItemCount < orderItemCount) {
					int takeItem = rng->next(100);
					if (takeItem > 50) {					
						multiValueItemNode* itemNode = TM_T::tmNew<multiValueItemNode>();
						itemNode->data = currItem->data;
						itemNode->next = orderItems;
						orderItems = itemNode;
						
						//get item value - to do with we do a point query on the itemId
						// we obviously could just store a pointer to the row in the category
						// table (rather than the itemId) -> we want to force the need for a query
						Row* itemRow = (Row*)indexManager.itemTable_itemId_index.get((SINGLE_COLUMN_KEY_T)currItem->data);
						assert(itemRow);
						orderValue += itemRow->columns[ITEM_PRICE];
						currOrderItemCount++;					
					}
					currItem = currItem->next;
				}

				if (currOrderItemCount == 0) {
					return false;
				}

				Row* new_order_table_row = TM_T::tmNew<Row>();
				new_order_table_row->columns[ORDER_ID] = orderId;
				new_order_table_row->columns[ORDER_CUSTOMER_ID] = customerId;
				new_order_table_row->columns[ORDER_VALUE] = orderValue;
				new_order_table_row->columns[ORDER_ITEMS] = (SINGLE_COLUMN_KEY_T)orderItems;
				new_order_table_row->columns[ORDER_DATE] = date;

				if (NO_VAL != indexManager.orderTable_orderId_index.addKey(orderId, (VAL_T)new_order_table_row)) {
					GSTATS_ADD(tid, failed_add_orderId_index, 1);
					return false;
				}

				multiValueItemNode* top = (multiValueItemNode*)indexManager.orderTable_date_index.get(date);
				multiValueItemNode* newValueNode = TM_T::tmNew<multiValueItemNode>();
				newValueNode->data = (void*)orderId;
				newValueNode->next = top;			
				indexManager.orderTable_date_index.update(date, newValueNode);
							
				TwoColumnKey customerIdAndDate = {customerId, date};	
				top = (multiValueItemNode*)indexManager.orderTable_customerId_date_index.get(customerIdAndDate);
				newValueNode = TM_T::tmNew<multiValueItemNode>();
				newValueNode->data = (void*)orderId;
				newValueNode->next = top;		
				indexManager.orderTable_customerId_date_index.update(customerIdAndDate, newValueNode);
				retVal = true;
				return true;
			});
		}
		
		return retVal;
	}

	//product search: pick random class, get list of items in that class, select randomly a subset of the items > lookup details of the items individually 
	uint64_t txn_multiItemLookup(const int tid, Random64* rng) {
		uint64_t garbage = 0;
		SINGLE_COLUMN_KEY_T category = 1+rng->next(CATEGORY_COUNT);

		TM_T::template readTx<bool>([&] () {    
			//get random category						
			void* categoryItems;
			indexManager.itemTable_categoryId_index.getSuccessorKey(category, &category, &categoryItems);

			// select random number of items from category
			multiValueItemNode* currItem = (multiValueItemNode*)categoryItems;
			int orderItemCount = 1+rng->next(MAX_ORDER_ITEM_COUNT);
			int currOrderItemCount = 0;			
			
			while (currItem && currOrderItemCount < orderItemCount) {
				int takeItem = rng->next(100);
				if (takeItem > 50) {
					//get item value - to do with we do a point query on the itemId
					// we obviously could just store a pointer to the row in the category
					// table (rather than the itemId) -> we want to force the need for a query
					Row* itemRow = (Row*)indexManager.itemTable_itemId_index.get((SINGLE_COLUMN_KEY_T)currItem->data);
					garbage += itemRow->columns[ITEM_PRICE];
					currOrderItemCount++;
				}
				currItem = currItem->next;
			}			
		});

		return garbage;
	}

	uint64_t txn_customerLookupOrderHistoryByDateRange(const int tid, Random64* rng) {
		SINGLE_COLUMN_KEY_T customerId = 1+rng->next(MAX_CUSTOMER_ID);
		SINGLE_COLUMN_KEY_T date = 1+rng->next(MAX_DATE);
		TwoColumnKey customerDateKeyLo = {customerId, date};
		TwoColumnKey customerDateKeyHi = {customerId, date};

		TwoColumnKey * rqResultKeys = new TwoColumnKey[RQSIZE+MAX_KEYS_PER_NODE]; 
    	VAL_T * rqResultValues = new VAL_T[RQSIZE+MAX_KEYS_PER_NODE]; 
		int rqResultSize;
		uint64_t garbage;

		TM_T::template readTx<bool>([&] () {    
			garbage = 0;
			rqResultSize = 0;
			void* orders;
			indexManager.orderTable_customerId_date_index.getSuccessorKey(customerDateKeyLo, &customerDateKeyLo, &orders);
			customerDateKeyHi = {customerDateKeyLo.col1, customerDateKeyLo.col2 + RQSIZE};
			rqResultSize = indexManager.orderTable_customerId_date_index.rangeQuery(customerDateKeyLo, customerDateKeyHi, rqResultKeys, rqResultValues);
			if (rqResultSize > RQSIZE+MAX_KEYS_PER_NODE) {
				printf("RQ SIZE ERROR\n");
				exit(-1);
			}
			
			for (int i = 0; i < rqResultSize; i++) {
				multiValueItemNode* currOrder = (multiValueItemNode*)rqResultValues[i];
				while (currOrder) {
					SINGLE_COLUMN_KEY_T orderId = (SINGLE_COLUMN_KEY_T)currOrder->data;
					Row* orderTableRow = (Row*)indexManager.orderTable_orderId_index.get(orderId);
					garbage += orderTableRow->columns[ORDER_VALUE];
					currOrder = currOrder->next;
				}				
			}
		});

		garbage = rqResultSize + 1;
		return garbage;
	}

	void txn_updateItem(const int tid, Random64* rng) {
		SINGLE_COLUMN_KEY_T itemId = 1+rng->next();
		VAL_T itemVal;
        uint64_t priceDelta = 1+rng->next(UPDATE_ITEM_PRICE_DELTA);
        uint64_t category = 1+rng->next(CATEGORY_COUNT);

		TM_T::template updateTx<bool>([&] () {    
			indexManager.itemTable_itemId_index.getSuccessorKey(itemId, &itemId, &itemVal);						
			Row* itemRow = (Row*)itemVal;
			itemRow->columns[ITEM_PRICE] += priceDelta;			
		});
	} 

	uint64_t txn_companySumOrderValueOverDateRange(const int tid, Random64* rng, SINGLE_COLUMN_KEY_T dateLo) {
		// SINGLE_COLUMN_KEY_T dateLo = 1+rng->next(MAX_DATE);
		SINGLE_COLUMN_KEY_T dateHi;
		VAL_T orders;

		SINGLE_COLUMN_KEY_T * rqResultKeys = new SINGLE_COLUMN_KEY_T[RQSIZE+MAX_KEYS_PER_NODE]; 
    	VAL_T * rqResultValues = new VAL_T[RQSIZE+MAX_KEYS_PER_NODE]; 

		uint64_t* countPerKey = new uint64_t[RQSIZE+MAX_KEYS_PER_NODE]; 

		int rqResultSize;
		uint64_t garbage;
		uint64_t count = 0;		

		TM_T::template readTx<bool>([&] () {
			garbage = 0;
			count = 0;  
			rqResultSize = 0;
			indexManager.orderTable_date_index.getSuccessorKey(dateLo, &dateLo, &orders);
			dateHi = dateLo + RQSIZE;
			rqResultSize = indexManager.orderTable_date_index.rangeQuery(dateLo, dateHi, rqResultKeys, rqResultValues);
			if (rqResultSize > RQSIZE+MAX_KEYS_PER_NODE) {
				printf("RQ SIZE ERROR\n");
				exit(-1);
			}
			
			// garbage += rqResultSize;

			// rqResultSize = 0;
			for (int i = 0; i < rqResultSize; i++) {
				multiValueItemNode* currOrder = (multiValueItemNode*)rqResultValues[i];
				countPerKey[i] = 0;
				while (currOrder) {
					SINGLE_COLUMN_KEY_T orderId = (SINGLE_COLUMN_KEY_T)currOrder->data;
					Row* orderTableRow = (Row*)indexManager.orderTable_orderId_index.get(orderId);
					assert(orderTableRow);
					garbage += orderTableRow->columns[ORDER_VALUE];
					currOrder = currOrder->next;
					count++;
					countPerKey[i]++;
				}
			}
		});

		// printf("dateLow=%ld\n", dateLo);
		// printf("dateHi=%ld\n", dateHi);
		// printf("%ld\n", count);
		// printf("%d\n", rqResultSize);
		// for (int i = 0; i < rqResultSize; i++) {
		// 	printf("%ld,", countPerKey[i]);
		// }
		// printf("\n");

		GSTATS_APPEND(tid, company_analytics_txn_point_queries, count);

		return garbage;
	}

	uint64_t txn_singleItemLookup(const int tid, Random64* rng) {
		uint64_t garbage = 0;
		SINGLE_COLUMN_KEY_T category = 1+rng->next(CATEGORY_COUNT);
		uint64_t itemId = 1+rng->next(MAX_ITEM_ID);

		TM_T::template readTx<bool>([&] () {
			Row* itemRow = (Row*)indexManager.itemTable_itemId_index.get(itemId);
			if (itemRow) {
				garbage += itemRow->columns[ITEM_PRICE];
			}		
		});

		return garbage;
	}

	// void txn_addMultipleItems(const int tid, Random64* rng) {
	// } 

private: 	
	// int (*cmpEqCol1SortCol2)(const TwoColumnKey&, const TwoColumnKey&) = [] (const TwoColumnKey& k1, const TwoColumnKey& k2) -> int {		
	// 	return 0;
	// }; 

	// static inline uint64_t hash_murmur3(uint64_t v) {
	// 	v ^= v>>33;
	// 	v *= 0xff51afd7ed558ccdLLU;
	// 	v ^= v>>33;
	// 	v *= 0xc4ceb9fe1a85ec53LLU;
	// 	v ^= v>>33;
	// 	return v;
	// }
};