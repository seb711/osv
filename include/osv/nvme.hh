#ifndef NVME_H_
#define NVME_H_

#include <functional>
#include <vector>
#include "nvme-structs.h"


#ifdef __cplusplus
extern "C" {
#endif

typedef void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl);

typedef struct osv_nvme_callback {
    osv_nvme_cmd_cb cb;
    void* cb_args;
} osv_nvme_callback;

std::vector<int> osv_get_available_ssds();

bool osv_shutdown_controller(int nvme_id); 

int osv_remove_io_user_queue(int nvme_id, void* queue);

void*osv_create_io_user_queue(int nvme_id, int queue_depth);

typedef std::function<int(int, void*, void*, uint64_t, uint32_t, void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl), void *, uint32_t)> leanstore_osv_rw_fn; 

int osv_nvme_nv_cmd_read(int, void*, void*, uint64_t, uint32_t, void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl), void *, uint32_t);
int osv_nvme_nv_cmd_write(int, void*, void*, uint64_t, uint32_t, void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl), void *, uint32_t);
int osv_nvme_qpair_process_completions(void*, uint32_t);

#ifdef __cplusplus
}
#endif


#endif
