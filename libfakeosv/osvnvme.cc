#include <osv/nvme.hh>
#include <functional>
#include <atomic>
#include <stdio.h>

extern "C" std::vector<int> osv_get_available_ssds() {
    return std::vector<int>(); 
}

extern "C" int osv_remove_io_user_queue(int nvme_id, void* queue) {
    return -1; 
}

extern "C" void* osv_create_io_user_queue(int nvme_id, int queue_depth) {
    printf("this should not be executed\n"); 
    return nullptr; 
}

typedef std::function<int(int, void*, void*, uint64_t, uint32_t, void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl), void *, uint32_t)> leanstore_osv_rw_fn; 

extern "C" int osv_nvme_nv_cmd_read(int, void*, void*, uint64_t, uint32_t, void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl), void *, uint32_t) {
    return -1; 
}

extern "C" int osv_nvme_nv_cmd_write(int, void*, void*, uint64_t, uint32_t, void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t* cpl), void *, uint32_t) {
    return -1; 
}

extern "C" int osv_nvme_qpair_process_completions(void*, uint32_t) {
    return -1; 
}

