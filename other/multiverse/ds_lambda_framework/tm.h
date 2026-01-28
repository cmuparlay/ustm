#ifdef USE_tinystm
	#include "TinySTM.hpp"
	#define TM_NS tinystm
	#define TM_T tinystm::TinySTM
	#define TM_TYPE_T tinystm::tx_field	
#elif defined(dcotl)
	#include "DCOTL.hpp"
	#define TM_NS ns_dcotl
	#define TM_T ns_dcotl::DCOTL
	#define TM_TYPE_T ns_dcotl::tx_field
#elif defined(multiverse)
	#include "multiverse.hpp"
	#define TM_NS ns_multiverse
	#define TM_T ns_multiverse::Multiverse
	#define TM_TYPE_T ns_multiverse::tx_field
#else
	#error "Must define a TM to use."
#endif