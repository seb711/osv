#ifndef SHARED_H_LEANSTORE_QUEUE
#define SHARED_H_LEANSTORE_QUEUE
#include <functional>
#include <atomic>

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

typedef struct benchmark_node_t {
  void* page; 
  benchmark_node_t* next; 

  benchmark_node_t(void* page) : page(page), next(nullptr) {}
} benchmark_node; 

typedef struct {
    uint64_t page_id;      
    uint64_t data_xor;         
    double time; 
    uint8_t padding[BENCHMARK_BLOCK_SIZE - sizeof(uint64_t) * 2  - sizeof(double)]; 
} benchmark_page_data_t;

typedef struct benchmark_page_t {
    unsigned long page_offset; 
    long page_count; 
    benchmark_page_data_t* data;
    NVME_COMMAND command; 
} benchmark_page;

typedef struct benchmark_metric_t {
  uint64_t xor_result = 0; 
  uint64_t write_ops = 0; 
  uint64_t read_ops = 0; 
  std::atomic<uint64_t> flushed{0}; 
} benchmark_metric;


typedef void (*osv_nvme_cmd_cb)(void *ctx, const nvme_sq_entry_t *cpl);
struct osv_nvme_callback {
    osv_nvme_cmd_cb cb;
    void* cb_args; 
}; 


extern "C" std::function<int(int)> leanstore_remove_io_user_queue __attribute__((weak));
extern "C" std::function<void*(int)> leanstore_create_io_user_queue __attribute__((weak));

extern "C" std::function<int(int, void*, void*, uint64_t, uint32_t, osv_nvme_cmd_cb, void *, uint32_t)> leanstore_osv_nvme_nv_cmd_read __attribute__((weak));
extern "C" std::function<int(int, void*, void*, uint64_t, uint32_t, osv_nvme_cmd_cb, void *, uint32_t)> leanstore_osv_nvme_nv_cmd_write __attribute__((weak));
extern "C" std::function<int(void*, uint32_t)> leanstore_osv_nvme_qpair_process_completions __attribute__((weak));

#endif // SHARED_H