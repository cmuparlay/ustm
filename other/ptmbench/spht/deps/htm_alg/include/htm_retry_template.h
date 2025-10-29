#ifndef HTM_SGL_RETRY_TEMPLATE_H_GUARD
#define HTM_SGL_RETRY_TEMPLATE_H_GUARD

#include "htm_arch.h"

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef HTM_SGL_INIT_BUDGET
#define HTM_SGL_INIT_BUDGET 20
#endif /* HTM_SGL_INIT_BUDGET */

typedef struct HTM_SGL_local_vars_ {
    int64_t budget;
    int64_t tid;
    int64_t status;
    uint64_t padding[5];
} __attribute__((packed))  HTM_SGL_local_vars_s;

// there is some wasted space near the SGL that can be used as read-only storate
extern void * HTM_read_only_storage1;
extern int HTM_read_only_storage1_size;
extern void * HTM_read_only_storage2;
extern int HTM_read_only_storage2_size;

extern __thread int64_t * volatile HTM_SGL_var_addr; // points to the prev
extern __thread HTM_SGL_local_vars_s CL_ALIGN HTM_SGL_vars;
extern __thread int64_t HTM_SGL_errors[HTM_NB_ERRORS];

#define START_TRANSACTION(status) (HTM_begin(status) != HTM_CODE_SUCCESS)
#define BEFORE_TRANSACTION(tid, budget) /* empty */
#define AFTER_TRANSACTION(tid, budget)  /* empty */

#define UPDATE_BUDGET(tid, budget, status) \
    HTM_inc_status_count(status); \
    HTM_INC(status); \
	budget = HTM_update_budget(budget, status)

/* The HTM_SGL_update_budget also handle statistics */

#define CHECK_SGL_NOTX() if (__atomic_load_n(HTM_SGL_var_addr, __ATOMIC_ACQUIRE) != -1) { HTM_block(); }
#define CHECK_SGL_HTM()  if (__atomic_load_n(HTM_SGL_var_addr, __ATOMIC_ACQUIRE) != -1) { HTM_abort(); }

#define AFTER_BEGIN(tid, budget, status)   /* empty */
#define BEFORE_COMMIT(tid, budget, status) /* empty */
#define COMMIT_TRANSACTION(tid, budget, status) \
    HTM_commit(); /* Commits and updates some statistics after */ \
    HTM_inc_status_count(status); \
    HTM_INC(status)

#define ENTER_SGL(tid) HTM_enter_fallback()
#define EXIT_SGL(tid)  HTM_exit_fallback()
#define AFTER_ABORT(tid, budget, status)   /* empty */

#define BEFORE_HTM_BEGIN(tid, budget)  /* empty */
#define AFTER_HTM_BEGIN(tid, budget)   /* empty */
#define BEFORE_SGL_BEGIN(tid)          /* empty */
#define AFTER_SGL_BEGIN(tid)           /* empty */

#define BEFORE_HTM_COMMIT(tid, budget) /* empty */
#define AFTER_HTM_COMMIT(tid, budget)  /* empty */
#define BEFORE_SGL_COMMIT(tid)         /* empty */
#define AFTER_SGL_COMMIT(tid)          /* empty */

#define BEFORE_CHECK_BUDGET(budget) /* empty */
// called within HTM_update_budget
#define HTM_UPDATE_BUDGET(budget, status) (budget - 1)

#define ENTER_HTM_COND(tid, budget) budget > 0
#define IN_TRANSACTION(tid, budget, status) \
	HTM_test()

// #################################
// Called within the API
#define HTM_INIT()       /* empty */
#define HTM_EXIT()       /* empty */
#define HTM_THR_INIT()   /* empty */
#define HTM_THR_EXIT()   /* empty */
#define HTM_INC(status)  /* Use this to construct side statistics */
// #################################

#define HTM_SGL_budget HTM_SGL_vars.budget
#define HTM_SGL_status HTM_SGL_vars.status
#define HTM_SGL_tid    HTM_SGL_vars.tid

#define HTM_SGL_begin() \
{ \
    HTM_SGL_budget = HTM_SGL_INIT_BUDGET; /* HTM_get_budget(); */ \
    BEFORE_TRANSACTION(HTM_SGL_tid, HTM_SGL_budget); \
    while (1) { \
        BEFORE_CHECK_BUDGET(HTM_SGL_budget); \
        if (ENTER_HTM_COND(HTM_SGL_tid, HTM_SGL_budget)) { \
            CHECK_SGL_NOTX(); \
            BEFORE_HTM_BEGIN(HTM_SGL_tid, HTM_SGL_budget); \
            if (START_TRANSACTION(HTM_SGL_status)) { \
                UPDATE_BUDGET(HTM_SGL_tid, HTM_SGL_budget, HTM_SGL_status); \
                AFTER_ABORT(HTM_SGL_tid, HTM_SGL_budget, HTM_SGL_status); \
                continue; \
            } \
            CHECK_SGL_HTM(); \
            AFTER_HTM_BEGIN(HTM_SGL_tid, HTM_SGL_budget); \
        } \
        else { \
            BEFORE_SGL_BEGIN(HTM_SGL_tid); \
            ENTER_SGL(HTM_SGL_tid); \
            AFTER_SGL_BEGIN(HTM_SGL_tid); \
        } \
        AFTER_BEGIN(HTM_SGL_tid, HTM_SGL_budget, HTM_SGL_status); \
        break; /* delete when using longjmp */ \
    } \
}
//
#define HTM_SGL_commit() \
{ \
    BEFORE_COMMIT(HTM_SGL_tid, HTM_SGL_budget, HTM_SGL_status); \
    if (IN_TRANSACTION(HTM_SGL_tid, HTM_SGL_budget, HTM_SGL_status)) { \
        BEFORE_HTM_COMMIT(HTM_SGL_tid, HTM_SGL_budget); \
        COMMIT_TRANSACTION(HTM_SGL_tid, HTM_SGL_budget, HTM_SGL_status); \
        AFTER_HTM_COMMIT(HTM_SGL_tid, HTM_SGL_budget); \
    } \
    else { \
        BEFORE_SGL_COMMIT(HTM_SGL_tid); \
        EXIT_SGL(HTM_SGL_tid); \
        AFTER_SGL_COMMIT(HTM_SGL_tid); \
    } \
    AFTER_TRANSACTION(HTM_SGL_tid, HTM_SGL_budget); \
} \

#define HTM_SGL_before_write(addr, val) /* empty */
#define HTM_SGL_after_write(addr, val)  /* empty */

#define HTM_SGL_write(addr, val) ({ \
	HTM_SGL_before_write(addr, val); \
	*((GRANULE_TYPE*)addr) = val; \
	HTM_SGL_after_write(addr, val); \
	val; \
})

#define HTM_SGL_write_D(addr, val) ({ \
	GRANULE_TYPE g = CONVERT_GRANULE_D(val); \
	HTM_SGL_write((GRANULE_TYPE*)addr, g); \
	val; \
})

#define HTM_SGL_write_P(addr, val) ({ \
	GRANULE_TYPE g = (GRANULE_TYPE) val; /* works for pointers only */ \
	HTM_SGL_write((GRANULE_TYPE*)addr, g); \
	val; \
})

#define HTM_SGL_before_read(addr) /* empty */

#define HTM_SGL_read(addr) ({ \
	HTM_SGL_before_read(addr); \
	*((GRANULE_TYPE*)addr); \
})

#define HTM_SGL_read_P(addr) ({ \
	HTM_SGL_before_read(addr); \
	*((GRANULE_P_TYPE*)addr); \
})

#define HTM_SGL_read_D(addr) ({ \
	HTM_SGL_before_read(addr); \
	*((GRANULE_D_TYPE*)addr); \
})

/* TODO: persistency assumes an identifier */
#define HTM_SGL_alloc(size) malloc(size)
#define HTM_SGL_free(pool) free(pool)

// Exposed API
#define HTM_init(nb_threads) HTM_init_(HTM_SGL_INIT_BUDGET, nb_threads)
void HTM_init_(int init_budget, int nb_threads);
void HTM_exit();
int HTM_thr_init(int); // pass -1 to get an id
void HTM_thr_exit();
void HTM_block();

// int HTM_update_budget(int budget, HTM_STATUS_TYPE status);
#define HTM_update_budget(budget, status) HTM_UPDATE_BUDGET(budget, status)
void HTM_enter_fallback();
void HTM_exit_fallback();

void HTM_inc_status_count(int status_code);
int HTM_get_nb_threads();
int HTM_get_tid();

// Getter and Setter for the initial budget
int HTM_get_budget();
void HTM_set_budget(int budget);

void HTM_set_is_record(int is_rec);
int HTM_get_is_record();
/**
 * @accum : int[nb_threads][HTM_NB_ERRORS]
 */
long HTM_get_status_count(int status_code, long **accum);
void HTM_reset_status_count();

#ifdef __cplusplus
}
#endif

#endif /* HTM_SGL_RETRY_TEMPLATE_H_GUARD */
