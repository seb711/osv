#include "nvme_connector.hh"

uint64_t ls_sq_phys_addr; 
uint64_t ls_cq_phys_addr; 

std::function<int(benchmark_page_t*, uint32_t)> make_request_page_ls;
std::function<void(benchmark_metric_t*)> req_done_ls;
