#ifndef SHARED_H_LEANSTORE_QUEUE
#define SHARED_H_LEANSTORE_QUEUE
#include <functional>
#include <atomic>
#include "drivers/nvme-structs.h"

#define NVME_IO_QUEUE_SIZE 32
#define BENCHMARK_BLOCK_SIZE 4096   // 4KB block size for random access

enum NVME_COMMAND {
  WRITE = 0,
  READ = 1,
  FLUSH = 2
}; 

extern uint64_t ls_sq_phys_addr; 
extern uint64_t ls_cq_phys_addr;  
extern uint32_t ls_qsize; 

typedef void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl);
struct osv_nvme_callback {
    osv_nvme_cmd_cb cb;
    void* cb_args; 

    osv_nvme_callback(osv_nvme_cmd_cb cb, void* cb_args) : cb(cb), cb_args(cb_args) {}; 
}; 

typedef std::function<int(int, void*, void*, uint64_t, uint32_t, void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl), void *, uint32_t)> leanstore_osv_rw_fn; 

extern "C" std::function<std::vector<int>()> leanstore_get_available_ssds __attribute__((weak));

extern "C" std::function<int(int, int)> leanstore_remove_io_user_queue __attribute__((weak));
extern "C" std::function<void*(int, int)> leanstore_create_io_user_queue __attribute__((weak));

extern "C" leanstore_osv_rw_fn leanstore_osv_nvme_nv_cmd_read __attribute__((weak));
extern "C" leanstore_osv_rw_fn leanstore_osv_nvme_nv_cmd_write __attribute__((weak));
extern "C" std::function<int(void*, uint32_t)> leanstore_osv_nvme_qpair_process_completions __attribute__((weak));

#endif // SHARED_H