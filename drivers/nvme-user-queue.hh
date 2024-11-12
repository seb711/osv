#ifndef NVME_USER_QUEUE_H
#define NVME_USER_QUEUE_H

#include "drivers/nvme-structs.h"
#include "drivers/nvme_connector/nvme_connector.hh"

#include <osv/virt_to_phys.hh>
#include <map> 
#include <queue>

#define nvme_tag "nvme"
#define nvme_d(...)    tprintf_d(nvme_tag, __VA_ARGS__)
#define nvme_i(...)    tprintf_i(nvme_tag, __VA_ARGS__)
#define nvme_w(...)    tprintf_w(nvme_tag, __VA_ARGS__)
#define nvme_e(...)    tprintf_e(nvme_tag, __VA_ARGS__)

#define NVME_ERROR(...) nvme_e(__VA_ARGS__)

#define NVME_PAGESIZE  mmu::page_size
#define NVME_PAGESHIFT 12

namespace nvme {
// Template to specify common elements of the submission and completion
// queue as described in the chapter 4.1 of the NVMe specification (see
// "https://www.nvmexpress.org/wp-content/uploads/NVM-Express-1_1a.pdf")
// The type T argument would be either nvme_sq_entry_t or nvme_cq_entry_t.
//
// The _tail, used by the producer, specifies the 0-based index of
// the next free slot to place new entry into the array _addr. After
// placing new entry, the _tail should be incremented - if it exceeds
// queue size, the it should roll to 0.
//
// The _head, used by the consumer, specifies the 0-based index of
// the entry to be fetched of the queue _addr. Likewise, the _head is
// incremented after, and if exceeds queue size, it should roll to 0.
//
// The queue is considered empty, if _head == _tail.
// The queue is considered full, if _head == (_tail + 1)
//
// The _doorbell points to the address where _tail of the submission
// queue is written to. For completion queue, it points to the address
// where the _head value is written to.
template<typename T>
struct queue {
    queue(u32* doorbell) :
        _addr(nullptr), _doorbell(doorbell), _head(0), _tail(0) {}
    T* _addr;
    volatile u32* _doorbell;
    std::atomic<u32> _head;
    u32 _tail;
};

class queue_pair
{
public:
    queue_pair(
        int driver_id,
        u32 id,
        int qsize,
        u32* sq_doorbell,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
    );

    ~queue_pair();

    u64 sq_phys_addr() { return (u64) mmu::virt_to_phys((void*) _sq._addr); }
    u64 cq_phys_addr() { return (u64) mmu::virt_to_phys((void*) _cq._addr); }

    virtual void req_done() {};
    bool completion_queue_not_empty() const;

    u32 _id;
    
protected:
    inline void advance_sq_tail();
    inline void advance_cq_head()
    {
        // trace_nvme_cq_head_advance(_driver_id, _id, _cq._head);
        if (++_cq._head == _qsize)
        {
            _cq._head = 0;
            _cq_phase_tag = _cq_phase_tag ? 0 : 1;
        }
    }

    u16 submit_cmd(nvme_sq_entry_t* cmd);

    nvme_cq_entry_t* get_completion_queue_entry();

    int _driver_id;

    // Length of the CQ and SQ
    // Admin queue is 8 entries long, therefore occupies 640 bytes (8 * (64 + 16))
    // I/O queue is normally 64 entries long, therefore occupies 5K (64 * (64 + 16))
    u32 _qsize;

    // Submission Queue (SQ) - each entry is 64 bytes in size
    queue<nvme_sq_entry_t> _sq;
    std::atomic<bool> _sq_full;

    // Completion Queue (CQ) - each entry is 16 bytes in size
    queue<nvme_cq_entry_t> _cq;
    u16 _cq_phase_tag;

    // Map of namespaces (for now there would normally be one entry keyed by 1)
    std::map<u32, nvme_ns_t*> _ns;

    static constexpr size_t max_pending_levels = 4;
};

class io_user_queue_pair : public queue_pair {
public:
    io_user_queue_pair(
        int driver_id,
        int id,
        int qsize,
        u32* sq_doorbell,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
    );
    ~io_user_queue_pair();

    int make_page_request(struct benchmark_page_t* bench_page, u32 nsid);

    void req_done();
    void req_done_page(benchmark_metric_t* result_xor); 

private:
    void init_pending_pages(u32 level); 

    inline u16 cid_to_row(u16 cid) { return cid / _qsize; }
    inline u16 cid_to_col(u16 cid) { return cid % _qsize; }


    u16 submit_read_write_page_cmd(u16 cid, u32 nsid, int opc, u64 slba, u32 nlb, struct benchmark_page_t *bench_page); 
    u16 submit_flush_cmd(u16 cid, u32 nsid);
    
    // Vector of arrays of pointers to struct bio used to track bio associated
    // with given command. The scheme to generate 16-bit 'cid' is -
    // _sq._tail + N * qsize - where N is typically 0 and  is equal
    // to a row in _pending_bios and _sq._tail is equal to a column.
    // Given cid, we can easily identify a pending bio by calculating
    // the row - cid / _qsize and column - cid % _qsize
    std::atomic<struct benchmark_page_t*>* _pending_pages[max_pending_levels] = {};
};

}

#endif
