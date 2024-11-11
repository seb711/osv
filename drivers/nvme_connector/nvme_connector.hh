#ifndef SHARED_H_LEANSTORE_QUEUE
#define SHARED_H_LEANSTORE_QUEUE
#include <functional>
#include <atomic>

#define NVME_IO_QUEUE_SIZE 64
#define BENCHMARK_BLOCK_SIZE 4096   // 4KB block size for random access

enum NVME_COMMAND {
  WRITE = 0,
  READ = 1,
  FLUSH = 2
}; 

extern uint64_t ls_sq_phys_addr; 
extern uint64_t ls_cq_phys_addr;  

typedef struct benchmark_node_t {
  void* page; 
  benchmark_node_t* next; 

  benchmark_node_t(void* page) : page(page), next(nullptr) {}
} benchmark_node; 

typedef struct {
    uint64_t page_id;      
    uint64_t data_xor;         
    uint8_t padding[BENCHMARK_BLOCK_SIZE - sizeof(uint64_t) * 2]; 
} benchmark_page_data_t;

typedef struct benchmark_page_t {
    unsigned long page_offset; 
    long page_count; 
    benchmark_page_data_t* data;
    NVME_COMMAND command; 
} benchmark_page;

typedef struct benchmark_metric_t {
  uint64_t xor_result =0; 
  uint64_t write_ops =0; 
  uint64_t read_ops =0;  
  std::atomic<uint64_t> flushed{0}; 
} benchmark_metric;

extern std::function<int(benchmark_page_t*, uint32_t)> make_request_page_ls;
extern std::function<void(benchmark_metric_t*)> req_done_ls;

#endif // SHARED_H