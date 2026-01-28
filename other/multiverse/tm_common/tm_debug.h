#pragma once

// #define DEBUG_TM_MODES 1

#ifdef DEBUG_TM_MODES
	#define DEBUG_PRINT_TM_MODES(...) printf(__VA_ARGS__);
	#define DEBUG_PRINT_TM(...) printf(__VA_ARGS__);
#else
	#define DEBUG_PRINT_TM_MODES(...)
	#define DEBUG_PRINT_TM(...) 
#endif