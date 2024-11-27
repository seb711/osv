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
#include <cassert>

#include <osv/contiguous_alloc.hh>
#include <osv/bio.h>
#include <osv/trace.hh>
#include <osv/mempool.hh>
#include <osv/mmio.hh>
#include <osv/align.hh>

#include "nvme-user-queue.hh"
#include "drivers/nvme_connector/nvme_connector.cpp"
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

    u16 queue_pair::submit_flush_cmd(u16 cid, u32 nsid)
    {
        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));

        cmd.vs.common.opc = NVME_CMD_FLUSH;
        cmd.vs.common.nsid = nsid;
        cmd.vs.common.cid = cid;

        return submit_cmd(&cmd);
    }

    io_user_queue_pair::io_user_queue_pair(
        int driver_id,
        int id,
        int qsize,
        u32 *sq_doorbell,
        u32 *cq_doorbell,
        std::map<u32, nvme_ns_t *> &ns) : queue_pair(driver_id,
                                                     id,
                                                     qsize,
                                                     sq_doorbell,
                                                     cq_doorbell,
                                                     ns)
    {
        init_pending_pages(max_pending_levels);
        init_callbacks(max_pending_levels);
    }

    io_user_queue_pair::~io_user_queue_pair()
    {
        for (auto page : _pending_pages)
        {
            if (page)
            {
                free(page);
            }
        }

        for (auto page : _pending_callbacks)
        {
            if (page)
            {
                free(page);
            }
        }
    }

    void io_user_queue_pair::init_pending_pages(u32 max_level)
    {
        for (u32 level = 0; level < max_level; level++)
        {
            _pending_pages[level] = (std::atomic<struct benchmark_page_t *> *)malloc(sizeof(std::atomic<struct benchmark_page_t *>) * _qsize);

            for (u32 idx = 0; idx < _qsize; idx++)
            {
                _pending_pages[level][idx] = nullptr;
            }
        }
    }

    void io_user_queue_pair::init_callbacks(u32 max_level)
    {
        for (u32 level = 0; level < max_level; level++)
        {
            _pending_callbacks[level] = (osv_nvme_callback *)malloc(sizeof(std::atomic<osv_nvme_callback>) * _qsize);

            for (u32 idx = 0; idx < _qsize; idx++)
            {
                _pending_callbacks[level][idx] = {nullptr, nullptr};
            }
        }
    }

    u16 io_user_queue_pair::submit_read_write_page_cmd(u16 cid, u32 nsid, int opc, u64 slba, u32 nlb, void* payload)
    {
        nvme_sq_entry_t cmd;
        memset(&cmd, 0, sizeof(cmd));

        cmd.rw.common.cid = cid;
        cmd.rw.common.opc = opc;
        cmd.rw.common.nsid = nsid;
        cmd.rw.slba = slba;   // starting logical block address
        cmd.rw.nlb = nlb - 1; // number of logical blocks

        void *data = (void *)mmu::virt_to_phys(payload);

        u64 addr = (u64)data;
        cmd.rw.common.prp1 = addr;
        cmd.rw.common.prp2 = 0;

        return submit_cmd(&cmd);
    }

    int io_user_queue_pair::make_page_request(struct benchmark_page_t *bench_page, u32 nsid = 1)
    {
        u64 slba = bench_page->page_offset;
        u32 nlb = bench_page->page_count; // do the blockshift in nvme_driver

        // SCOPE_LOCK(_lock);
        // u8 counter = 0; 
        if (_sq_full)
        {
            // counter++; 
            // if (counter > 10) assert(false); 
            // process_completions(24); 
            return 0; 
        }

        assert(!_sq_full); 
        assert((((_sq._tail + 1) % _qsize) != _sq._head)); // one left 
        
        //
        // We need to check if there is an outstanding command that uses
        // _sq._tail as command id.
        // This happens if:
        // 1. The SQ is full. Then we just have to wait for an open slot (see above)
        // 2. The Controller already read a SQE but didnt post a CQE yet.
        //    This means we could post the command but need a different cid. To still
        //    use the cid as index to find the corresponding bios we use a matrix
        //    adding columns if we need them
        u16 cid = _sq._tail;
        while (_pending_pages[cid_to_row(cid)][cid_to_col(cid)].load())
        {
            cid += _qsize;
            auto level = cid_to_row(cid);
            if (level >= max_pending_levels)
                return 0; // this should never be the case
        } 
        /*
        CID thing is weird... maybe we skip that

        while (_pending_pages[cid_to_row(cid)][cid_to_col(cid)].load())
        {
            trace_nvme_cid_conflict(_driver_id, _id, cid);
            cid += _qsize;
            auto level = cid_to_row(cid);
            assert(level < max_pending_levels);
            // Allocate next row of _pending_bios if needed
            if (!_pending_pages[cid_to_row(cid)])
            {
                init_pending_pages(level);
            }
        } */

        // Save bio
        _pending_pages[cid_to_row(cid)][cid_to_col(cid)] = bench_page;

        switch (bench_page->command)
        {
        case NVME_COMMAND::READ:
        {
            submit_read_write_page_cmd(cid, nsid, NVME_CMD_READ, slba, nlb, bench_page->data);
            break;
        }
        case NVME_COMMAND::WRITE:
        {
            submit_read_write_page_cmd(cid, nsid, NVME_CMD_WRITE, slba, nlb,  bench_page->data);
            break;
        }
        case NVME_COMMAND::FLUSH:
        {
            submit_flush_cmd(cid, nsid);
            break;
        }
        default:
        {
            NVME_ERROR("Operation not implemented\n");
            return ENOTBLK;
        }
        }

        return 1;
    }

    int io_user_queue_pair::submit_request(int ns, void *payload, uint64_t lba, uint32_t lba_count, osv_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t io_flags, NVME_COMMAND cmd)
    {
        u64 slba = lba;
        u32 nlb = lba_count; // do the blockshift in nvme_driver

        // SCOPE_LOCK(_lock);
        // u8 counter = 0; 
        if (_sq_full)
        {
            // counter++; 
            // if (counter > 10) assert(false); 
            // process_completions(10); 
            // std::cout << "full" << std::endl; 
            return 0; 
        }

        assert(!_sq_full); 
        assert((((_sq._tail + 1) % _qsize) != _sq._head)); // one left 
        
        //
        // We need to check if there is an outstanding command that uses
        // _sq._tail as command id.
        // This happens if:
        // 1. The SQ is full. Then we just have to wait for an open slot (see above)
        // 2. The Controller already read a SQE but didnt post a CQE yet.
        //    This means we could post the command but need a different cid. To still
        //    use the cid as index to find the corresponding bios we use a matrix
        //    adding columns if we need them
        u16 cid = _sq._tail;
        while (_pending_callbacks[cid_to_row(cid)][cid_to_col(cid)].cb)
        {
            cid += _qsize;
            auto level = cid_to_row(cid);
            if (level >= max_pending_levels)
                return 0; // this should never be the case
        } 
        /*
        CID thing is weird... maybe we skip that

        while (_pending_pages[cid_to_row(cid)][cid_to_col(cid)].load())
        {
            trace_nvme_cid_conflict(_driver_id, _id, cid);
            cid += _qsize;
            auto level = cid_to_row(cid);
            assert(level < max_pending_levels);
            // Allocate next row of _pending_bios if needed
            if (!_pending_pages[cid_to_row(cid)])
            {
                init_pending_pages(level);
            }
        } */

        // Save bio
        _pending_callbacks[cid_to_row(cid)][cid_to_col(cid)] = {cb_fn, cb_arg};

        switch (cmd)
        {
        case NVME_COMMAND::READ:
        {
            submit_read_write_page_cmd(cid, ns, NVME_CMD_READ, slba, nlb, payload);
            break;
        }
        case NVME_COMMAND::WRITE:
        {
            assert((u64)((uint8_t *)payload)[16] == (u64)((uint8_t *)payload)[4088]);
            submit_read_write_page_cmd(cid, ns, NVME_CMD_WRITE, slba, nlb, payload);
            break;
        }
        case NVME_COMMAND::FLUSH:
        {
            submit_flush_cmd(cid, ns);
            break;
        }
        default:
        {
            NVME_ERROR("Operation not implemented\n");
            return ENOTBLK;
        }
        }

        return 1;
    }

    void io_user_queue_pair::req_done_page(benchmark_metric_t *result_xor)
    {
        nvme_cq_entry_t *cqep = nullptr;

        // std::cout << "start" << std::endl; 

        while ((cqep = get_completion_queue_entry()))
        {
            // Read full CQ entry onto stack so we can advance CQ head ASAP
            // and release the CQ slot
            nvme_cq_entry_t cqe = *cqep;

            advance_cq_head();
            mmio_setl(_cq._doorbell, _cq._head); // TODO maybe do this in batches (more efficient?)

            // Wake up the requesting thread in case the submission queue was full before

            auto old_sq_head = _sq._head;
            _sq._head = cqe.sqhd; 

            if (old_sq_head != cqe.sqhd && _sq_full)
            {
                _sq_full = false;
            }

            // do here some logic -> prob best would be to just take that bio read it and then next
            // Read cid and release it
            u16 cid = cqe.cid;
            auto pending_page = _pending_pages[cid_to_row(cid)][cid_to_col(cid)].exchange(nullptr);
            assert(pending_page);

            switch (pending_page->command)
            {
            case NVME_COMMAND::READ:
            {
                result_xor->read_ops++;
                result_xor->xor_result ^= ((benchmark_page_t*) pending_page)->data->data_xor;
                break;
            }
            case NVME_COMMAND::WRITE:
            {
                result_xor->write_ops++;
            }
            case NVME_COMMAND::FLUSH:
            {
                result_xor->flushed = 1;
            }
            default:
            {
            }
            }
        }

        // std::cout << "end" << std::endl; 
    }

    //  returns number of completions processed (may be 0) or negated on error. -ENXIO in the special case that the qpair is failed at the transport layer. 
    int io_user_queue_pair::process_completions(int max) // Process any outstanding completions for I/O submitted on a queue pair. 
    {
        nvme_cq_entry_t *cqep = nullptr;
        int counter = 0;

        max = (max > 0) ? max : _qsize; 

        while (counter < max)
        {
            if ((cqep = get_completion_queue_entry()))
            {
                // Read full CQ entry onto stack so we can advance CQ head ASAP
                // and release the CQ slot
                nvme_cq_entry_t cqe = *cqep;
                
                advance_cq_head();
                mmio_setl(_cq._doorbell, _cq._head); // TODO maybe do this in batches (more efficient?


                // Wake up the requesting thread in case the submission queue was full before
                auto old_sq_head = _sq._head;
                _sq._head = cqe.sqhd; 

                assert(cqe.sc == 0); 


                if (old_sq_head != cqe.sqhd && _sq_full)
                {
                    _sq_full = false;
                }

                // do here some logic -> prob best would be to just take that bio read it and then next
                // Read cid and release it
                u16 cid = cqe.cid;
                // auto pending_page = _pending_pages[cid_to_row(cid)][cid_to_col(cid)].exchange(nullptr);
                // assert(pending_page);

                // struct timespec start;
                // clock_gettime(CLOCK_MONOTONIC, &start);
                // pending_page->data->time = start.tv_sec + (start.tv_nsec) / 1e9;

                auto pending_callback = _pending_callbacks[cid_to_row(cid)][cid_to_col(cid)];
                _pending_callbacks[cid_to_row(cid)][cid_to_col(cid)] = {nullptr, nullptr};
                pending_callback.cb(pending_callback.cb_args, 0);

                // FIXME: HERE INSERT CALLING THE CALLBACK OF THE pending_page
                counter++;
            } else {
                // here should the doorbell be hit
                // mmio_setl(_cq._doorbell, _cq._head);

                return counter; 
            }
        }

        // here should the doorbell be hit
        // mmio_setl(_cq._doorbell, _cq._head);

        return counter;
    }

    int osv_nvme_nv_cmd_read(int ns, void* queue, void *payload, uint64_t lba, uint32_t lba_count, nvme::osv_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t io_flags) {
    // read stuff
    nvme::io_user_queue_pair* queuet = (nvme::io_user_queue_pair*) queue;
    int success = 0; 
    do {
        success = queuet->submit_request(ns,  payload,  lba, lba_count, cb_fn, cb_arg, io_flags, NVME_COMMAND::READ); 
        if (!success) {
            queuet->process_completions(5); 
        }
    } while (!success); 
    return 0; 
    }

    int osv_nvme_nv_cmd_write(int ns, void* queue, void *payload, uint64_t lba, uint32_t lba_count, nvme::osv_nvme_cmd_cb cb_fn, void *cb_arg, uint32_t io_flags) {
        // read stuff
        nvme::io_user_queue_pair* queuet = (nvme::io_user_queue_pair*) queue;

        int success = 0; 
        do {
            success = queuet->submit_request(ns,  payload,  lba, lba_count, cb_fn, cb_arg, io_flags, NVME_COMMAND::WRITE); 

            if (!success) {
                queuet->process_completions(5); 
            }
        } while (!success); 
        return 0; 
    }

    int osv_nvme_qpair_process_completions( void* queue, uint32_t max_completions) {
        nvme::io_user_queue_pair* queuet = (nvme::io_user_queue_pair*) queue;

        return queuet->process_completions(max_completions); 
    }
};