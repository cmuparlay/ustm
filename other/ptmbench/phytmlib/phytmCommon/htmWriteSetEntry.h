#pragma once

#include <stdint.h>

struct htmWriteSetEntry {
	volatile intptr_t* addr;
	intptr_t old;
};