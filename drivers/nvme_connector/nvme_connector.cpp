#include "nvme_connector.hh"

std::function<std::vector<int>()> leanstore_get_available_ssds;

std::function<int(int, int)> leanstore_remove_io_user_queue;
std::function<void*(int, int)> leanstore_create_io_user_queue;

std::function<int(int, void*, void*, uint64_t, uint32_t, osv_nvme_cmd_cb, void *, uint32_t)> leanstore_osv_nvme_nv_cmd_read;
std::function<int(int, void*, void*, uint64_t, uint32_t, osv_nvme_cmd_cb, void *, uint32_t)> leanstore_osv_nvme_nv_cmd_write;
std::function<int(void*, uint32_t)> leanstore_osv_nvme_qpair_process_completions;
