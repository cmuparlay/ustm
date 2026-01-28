#pragma once

#ifndef VOLATILE_MEMORY_REGION_SIZE
	#define VOLATILE_MEMORY_REGION_SIZE 128*1024*1024*1024ULL //GB
#endif

#ifdef USE_MULTIVERSE
  	#define TM_NAME "Multiverse"
	#include "multiverse.hpp"
  	#define TM_NS ns_multiverse
	#define TM_T ns_multiverse::Multiverse
	#define TM_TYPE_T ns_multiverse::tx_field
#elif defined(USE_DCTL)
	#define TM_NAME "DCTL"
	#include "DCOTL.hpp"
	#define TM_NS ns_dcotl
	#define TM_T TM_NS::DCOTL
	#define TM_TYPE_T TM_NS::tx_field
#else
	#define TM_NAME "Default TM: DCTL"
	#include "DCOTL.hpp"
	#define TM_NS ns_dcotl
	#define TM_T TM_NS::DCOTL
	#define TM_TYPE_T TM_NS::tx_field
#endif

