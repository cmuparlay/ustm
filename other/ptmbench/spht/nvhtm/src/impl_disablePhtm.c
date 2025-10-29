#include "impl.h"

void on_before_htm_begin_disable_phtm(int threadId) {
	onBeforeWrite = on_before_htm_write;
  	onBeforeHtmCommit = on_before_htm_commit;
}
void on_htm_abort_disable_phtm(int threadId) {
}
void on_before_htm_commit_disable_phtm(int threadId) {
}
void on_after_htm_commit_disable_phtm(int threadId) {
}
void wait_commit_disable_phtm(int threadId) {
}

void on_before_htm_write_8B__disable_phtm(int threadId, void *addr, uint64_t val) {
}
