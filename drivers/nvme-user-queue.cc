/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/cdefs.h>

#include <vector>
#include <memory>
#include <iostream>

#include <osv/contiguous_alloc.hh>
#include <osv/bio.h>
#include <osv/trace.hh>
#include <osv/mempool.hh>
#include <osv/mmio.hh>
#include <osv/align.hh>

#include "nvme-user-queue.hh"
#include <queue>

TRACEPOINT(trace_nvme_cq_wait, "nvme%d qid=%d, cq_head=%d", int, int, int);
TRACEPOINT(trace_nvme_cq_woken, "nvme%d qid=%d, have_elements=%d", int, int, bool);
TRACEPOINT(trace_nvme_cq_not_empty, "nvme%d qid=%d, not_empty=%d", int, int, bool);
TRACEPOINT(trace_nvme_cq_head_advance, "nvme%d qid=%d cq_head=%d", int, int, int);
TRACEPOINT(trace_nvme_cq_new_entry, "nvme%d qid=%d sqhd=%d", int, int, int);

TRACEPOINT(trace_nvme_enable_interrupts, "nvme%d qid=%d", int, int);
TRACEPOINT(trace_nvme_disable_interrupts, "nvme%d qid=%d", int, int);

TRACEPOINT(trace_nvme_req_done_error, "nvme%d qid=%d, cid=%d, status type=%#x, status code=%#x, bio=%p", int, int, u16, u8, u8, bio *);
TRACEPOINT(trace_nvme_req_done_success, "nvme%d qid=%d, cid=%d, bio=%p", int, int, u16, bio *);

TRACEPOINT(trace_nvme_admin_cmd_submit, "nvme%d qid=%d, cid=%d, opc=%d", int, int, int, u8);
TRACEPOINT(trace_nvme_read_write_cmd_submit, "nvme%d qid=%d cid=%d, bio=%p, slba=%d, nlb=%d, write=%d", int, int, u16, void *, u64, u32, bool);

TRACEPOINT(trace_nvme_sq_tail_advance, "nvme%d qid=%d, sq_tail=%d, sq_head=%d, depth=%d, full=%d", int, int, int, int, int, bool);
TRACEPOINT(trace_nvme_sq_full_wait, "nvme%d qid=%d, sq_tail=%d, sq_head=%d", int, int, int, int);
TRACEPOINT(trace_nvme_sq_full_wake, "nvme%d qid=%d, sq_tail=%d, sq_head=%d", int, int, int, int);

TRACEPOINT(trace_nvme_cid_conflict, "nvme%d qid=%d, cid=%d", int, int, int);

TRACEPOINT(trace_nvme_prp_alloc, "nvme%d qid=%d, prp=%p", int, int, void *);
TRACEPOINT(trace_nvme_prp_free, "nvme%d qid=%d, prp=%p", int, int, void *);


using namespace memory;

namespace nvme
{
    queue_pair::queue_pair(
        int did,
        u32 id,
        int qsize,
        u32 *sq_doorbell,
        u32 *cq_doorbell,
        std::map<u32, nvme_ns_t *> &ns)
        : _id(id), _driver_id(did), _qsize(qsize), _sq(sq_doorbell), _sq_full(false), _cq(cq_doorbell), _cq_phase_tag(1), _ns(ns)
    {
        size_t sq_buf_size = qsize * sizeof(nvme_sq_entry_t);
        _sq._addr = (nvme_sq_entry_t *)alloc_phys_contiguous_aligned(sq_buf_size, mmu::page_size);
        assert(_sq._addr);
        memset(_sq._addr, 0, sq_buf_size);

        size_t cq_buf_size = qsize * sizeof(nvme_cq_entry_t);
        _cq._addr = (nvme_cq_entry_t *)alloc_phys_contiguous_aligned(cq_buf_size, mmu::page_size);
        assert(_cq._addr);
        memset(_cq._addr, 0, cq_buf_size);

        assert(!completion_queue_not_empty());
    }

    queue_pair::~queue_pair()
    {
        free_phys_contiguous_aligned(_sq._addr);
        free_phys_contiguous_aligned(_cq._addr);
    }

    inline void queue_pair::advance_sq_tail()
    {
        _sq._tail = (_sq._tail + 1) % _qsize;
        if (((_sq._tail + 1) % _qsize) == _sq._head)
        {
            _sq_full = true;
        }
        trace_nvme_sq_tail_advance(_driver_id, _id, _sq._tail, _sq._head,
                                   (_sq._tail >= _sq._head) ? _sq._tail - _sq._head : _sq._tail + (_qsize - _sq._head),
                                   _sq_full);
    }


    u16 queue_pair::submit_cmd(nvme_sq_entry_t *cmd)
    {
        _sq._addr[_sq._tail] = *cmd;
        advance_sq_tail();
        mmio_setl(_sq._doorbell, _sq._tail);
        return _sq._tail;
    }
    
    nvme_cq_entry_t *queue_pair::get_completion_queue_entry()
    {
        if (!completion_queue_not_empty())
        {
            return nullptr;
        }

        auto *cqe = &_cq._addr[_cq._head];
        assert(cqe->p == _cq_phase_tag);

        trace_nvme_cq_new_entry(_driver_id, _id, cqe->sqhd);
        return cqe;
    }

    bool queue_pair::completion_queue_not_empty() const
    {
        bool a = reinterpret_cast<volatile nvme_cq_entry_t *>(&_cq._addr[_cq._head])->p == _cq_phase_tag;
        trace_nvme_cq_not_empty(_driver_id, _id, a);
        return a;
    }
}; 