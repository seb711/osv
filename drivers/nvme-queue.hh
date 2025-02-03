/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef NVME_QUEUE_H
#define NVME_QUEUE_H

#include "drivers/pci-device.hh"
#include "drivers/nvme-user-queue.hh"

#include <osv/bio.h>


namespace nvme {
// Pair of submission queue and completion queue - SQ and CQ.
// They work in tandem and share the same size.
class queue_interrupt_pair : public queue_pair
{
public:
    queue_interrupt_pair(
        int driver_id,
        u32 id,
        int qsize,
        pci::device& dev,
        u32* sq_doorbell,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
    );

    ~queue_interrupt_pair();

    virtual void req_done() {};

    void wait_for_completion_queue_entries();

    void enable_interrupts();
    void disable_interrupts();

protected:

    // PRP stands for Physical Region Page and is used to specify locations in
    // physical memory for data tranfers. In essence, they are arrays of physical
    // addresses of pages to read from or write to data.
    void map_prps(nvme_sq_entry_t* cmd, struct bio* bio, u64 datasize);

    int _driver_id;

    pci::device* _dev;

    mutex _lock;
};

// Pair of SQ and CQ queues used for reading from and writing to (I/O)
class io_queue_pair : public queue_interrupt_pair {
public:
    io_queue_pair(
        int driver_id,
        int id,
        int qsize,
        pci::device& dev,
        u32* sq_doorbell,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
    );
    ~io_queue_pair();

    int make_request(struct bio* bio, u32 nsid);

    void req_done();

private:
    void init_pending_bios(u32 level);

    inline u16 cid_to_row(u16 cid) { return cid / _qsize; }
    inline u16 cid_to_col(u16 cid) { return cid % _qsize; }

    u16 submit_read_write_cmd(u16 cid, u32 nsid, int opc, u64 slba, u32 nlb, struct bio* bio);

    sched::thread_handle _sq_full_waiter;

    // Vector of arrays of pointers to struct bio used to track bio associated
    // with given command. The scheme to generate 16-bit 'cid' is -
    // _sq._tail + N * qsize - where N is typically 0 and  is equal
    // to a row in _pending_bios and _sq._tail is equal to a column.
    // Given cid, we can easily identify a pending bio by calculating
    // the row - cid / _qsize and column - cid % _qsize
    std::atomic<struct bio*>* _pending_bios[max_pending_levels] = {};
};

// Pair of SQ and CQ queues used for setting up/configuring controller
// like creating I/O queues
class admin_queue_pair : public queue_interrupt_pair {
public:
    admin_queue_pair(
        int driver_id,
        int id,
        int qsize,
        pci::device& dev,
        u32* sq_doorbell,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
    );

    void req_done();
    nvme_cq_entry_t submit_and_return_on_completion(nvme_sq_entry_t* cmd, void* data = nullptr, unsigned int datasize = 0);
private:
    sched::thread_handle _req_waiter;
    nvme_cq_entry_t _req_res;
    volatile bool new_cq;
};

}

#endif
