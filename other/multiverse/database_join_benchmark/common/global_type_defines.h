#pragma once

#define SINGLE_COLUMN_KEY_T uint64_t
#define VAL_T void*

struct TwoColumnKey {
	SINGLE_COLUMN_KEY_T col1;
	SINGLE_COLUMN_KEY_T col2;

	TwoColumnKey() {
		col1 = 0;
		col2 = 0;
	}

	TwoColumnKey(const TwoColumnKey &other) {
		col1 = other.col1;
		col2 = other.col2;
	}

	TwoColumnKey(SINGLE_COLUMN_KEY_T otherCol1, SINGLE_COLUMN_KEY_T otherCol2) {
		col1 = otherCol1;
		col2 = otherCol2;
	}
};