#pragma once

#ifndef SOFTWARE_BARRIER
    #define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

struct mcs_node {
    volatile char padding0[192];
    mcs_node * volatile next;
    volatile bool locked;
    volatile char padding1[192];
};

typedef struct mcs_node * volatile mcs_lock;

void mcs_acquire(mcs_lock * lock, mcs_node * self) {
    self->next = NULL;
    mcs_node * pred = NULL;
    __atomic_exchange(lock, &self, &pred, __ATOMIC_SEQ_CST);
    if (pred) {
        self->locked = true;
        SOFTWARE_BARRIER;
        pred->next = self;
        // __atomic_store(&pred->next, &self, __ATOMIC_SEQ_CST);
        while (self->locked) {}
    }
}

void mcs_release(mcs_lock * lock, mcs_node * self) {
    if (!self->next) {
        mcs_node * expected = self;
        if (__atomic_compare_exchange_n(lock, &expected, NULL,
                false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return;
        // if (__sync_bool_compare_and_swap(lock, self, NULL)) return;
        while (!self->next) {}
    }
    self->next->locked = false;
}
