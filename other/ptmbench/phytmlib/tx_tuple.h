#pragma once

template <class vlockType>
struct tx_tuple {
    uintptr_t volatile bits;
#ifdef PHYTM_COLOCATED_LOCKS
    vlockType vlock; 
#endif
    // uintptr_t volatile old;
    // uintptr_t volatile pversionNum;
    // uintptr_t volatile txId;
    // PAD;
};