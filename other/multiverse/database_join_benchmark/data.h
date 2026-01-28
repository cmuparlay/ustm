#pragma once

#include "common/global_tm_defines.h"
#include "common/global_type_defines.h"

enum {
	ITEM_ID,
	ITEM_PRICE,
	ITEM_CATEGORY,
	MAX_ITEM_TABLE_COL_COUNT
};

enum {
	ORDER_ID,
	ORDER_CUSTOMER_ID,
	ORDER_ITEMS,
	ORDER_VALUE,
	ORDER_DATE,
	MAX_ORDER_TABLE_COL_COUNT
};

static constexpr int MAX_COL_COUNT = (int)MAX_ORDER_TABLE_COL_COUNT > (int)MAX_ITEM_TABLE_COL_COUNT ? (int)MAX_ORDER_TABLE_COL_COUNT : (int)MAX_ITEM_TABLE_COL_COUNT;

struct Row {
	TM_TYPE_T<SINGLE_COLUMN_KEY_T> columns[MAX_COL_COUNT];
};

struct multiValueItemNode {
	void* data;
	multiValueItemNode* next;	
};


struct tx_multiValueItemNode {
	void* data;
	TM_TYPE_T<tx_multiValueItemNode*> next;	
};

struct tx_multiValueItemList {
	TM_TYPE_T<tx_multiValueItemNode*> head;
	TM_TYPE_T<tx_multiValueItemNode*> tail;
};

struct unsafe_multiValueItemNode {
	void* data;
	unsafe_multiValueItemNode* next;	
};

// struct unsafe_multiValueItemList {
// 	unsafe_multiValueItemNode* head;
// 	unsafe_multiValueItemNode* tail;
// };