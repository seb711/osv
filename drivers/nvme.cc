/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/cdefs.h>

#include "drivers/nvme.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>

#include <cassert>
#include <sstream>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>
#include <iostream>
#include <functional>

#include <osv/sched.hh>
#include <osv/trace.hh>
#include <osv/aligned_new.hh>

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/ioctl.h>
#include <osv/contiguous_alloc.hh>
#include <osv/aligned_new.hh>

using namespace memory;

#include <sys/mman.h>
#include <sys/refcount.h>

#include <osv/drivers_config.h>

TRACEPOINT(trace_nvme_strategy, "bio=%p, bcount=%lu", struct bio *, size_t);

#define QEMU_VID 0x1b36



namespace nvme
{

    int driver::_disk_idx = 1;
    int driver::_instance = 0;
    driver* driver::prev_nvme_driver = nullptr; 

    struct nvme_priv
    {
        devop_strategy_t strategy;
        driver *drv;
        u32 nsid;
    };

    [[maybe_unused]]  static void nvme_strategy(struct bio *bio)
    {
        auto *prv = reinterpret_cast<struct nvme_priv *>(bio->bio_dev->private_data);
        trace_nvme_strategy(bio, bio->bio_bcount);
        prv->drv->make_request(bio);
    }

    static int
    nvme_read(struct device *dev, struct uio *uio, int io_flags)
    {
        return bdev_read(dev, uio, io_flags);
    }

    static int
    nvme_write(struct device *dev, struct uio *uio, int io_flags)
    {
        return bdev_write(dev, uio, io_flags);
    }

    static int
    nvme_open(struct device *dev, int ioflags)
    {
        return 0;
    }

    static struct devops nvme_devops
    {
        nvme_open,
            no_close,
            nvme_read,
            nvme_write,
            no_ioctl,
            no_devctl,
            multiplex_strategy,
    };

    struct ::driver _driver = {
        "nvme",
        &nvme_devops,
        sizeof(struct nvme_priv),
    };

    static void setup_features_cmd(nvme_sq_entry_t *cmd, u8 feature_id, u32 val)
    {
        memset(cmd, 0, sizeof(nvme_sq_entry_t));
        cmd->set_features.common.opc = NVME_ACMD_SET_FEATURES;
        cmd->set_features.fid = feature_id;
        cmd->set_features.val = val;
    }

    enum CMD_IDENTIFY_CNS
    {
        CMD_IDENTIFY_NAMESPACE = 0,
        CMD_IDENTIFY_CONTROLLER = 1,
    };

#define NVME_NAMESPACE_DEFAULT_NS 1

    static void setup_identify_cmd(nvme_sq_entry_t *cmd, u32 namespace_id, u32 cns)
    {
        memset(cmd, 0, sizeof(nvme_sq_entry_t));
        cmd->identify.common.opc = NVME_ACMD_IDENTIFY;
        cmd->identify.common.nsid = namespace_id;
        cmd->identify.cns = cns;
    }

    driver::driver(pci::device &pci_dev)
        : _dev(pci_dev), _msi(&pci_dev)
    {
        auto parse_ok = parse_pci_config();
        assert(parse_ok);
    
        enable_msix();
    
        _id = _instance++;
    
        _doorbell_stride = 1 << (2 + _control_reg->cap.dstrd);
        _qsize = (NVME_IO_QUEUE_SIZE < _control_reg->cap.mqes) ? NVME_IO_QUEUE_SIZE : _control_reg->cap.mqes + 1;

        assert(enable_disable_controller(true) == 0);

        // Wait for controller to become ready
        assert(wait_for_controller_ready_change(1) == 0);
    
        //Disable controller
        assert(enable_disable_controller(false) == 0);
    
        init_controller_config();
    
        create_admin_queue();
    
        //Enable controller
        assert(enable_disable_controller(true) == 0);
    
        assert(identify_controller() == 0);
    
        assert(identify_namespace(NVME_NAMESPACE_DEFAULT_NS) == 0);
    
        //Enable write cache if available
        if (_identify_controller->vwc & 0x1 && NVME_VWC_ENABLED) {
            enable_write_cache();
        }

#ifdef USE_COALESCING
        set_interrupt_coalescing(8, 2); // 200us coalescing or >=8 completed entries
#endif
    

        /* 
        // UNCOMMENT THIS BLOCK IF YOU WANT TO USE THE FULL DRIVE AS A OS DEVICE
        //Create IO queues
        create_io_queues();
    
        if (_identify_controller->vid != QEMU_VID) {
            set_interrupt_coalescing(20, 2); // 200us coalescing
        }
    
        std::string dev_name("vblk");
        dev_name += std::to_string(_disk_idx++);
    
        struct device* dev = device_create(&_driver, dev_name.c_str(), D_BLK);
        struct nvme_priv* prv = reinterpret_cast<struct nvme_priv*>(dev->private_data);
    
        unsigned int nsid = NVME_NAMESPACE_DEFAULT_NS;
        const auto& ns = _ns_data[nsid];
        off_t size = ((off_t) ns->blockcount) << ns->blockshift;
    
        prv->strategy = nvme_strategy;
        prv->drv = this;
        prv->nsid = nsid;
        dev->size = size;
        //IO size greater than 4096 << 9 would mean we need
        //more than 1 page for the prplist which is not implemented
        dev->max_io_size = mmu::page_size << ((9 < _identify_controller->mdts)? 9 : _identify_controller->mdts); 
        read_partition_table(dev);
        */
        
        // if (_disk_idx == 0)

        // if (_dev.get_vendor_id() != 0x144d)
        // if (false)
        // {
            /* 

            debugf("nvme: Add device instances %d as %s, devsize=%lld, serial number:%s\n",
                   _id, dev_name.c_str(), dev->size, _identify_controller->sn);
            std::cout << "connected to OS vblk" << _disk_idx << std::endl; */
        // } else {
            create_io_user_queue_endpoints(); 
        /*
        nvme_sq_entry_t cmd;
        setup_features_cmd(&cmd, NVME_FEATURE_POWER_MGMT, 0);
        auto res = _admin_queue->submit_and_return_on_completion(&cmd);

        typedef union _nvme_power_mgmt {
            u32                 val;            ///< whole value
            struct {
                u32             ps      : 5;    ///< base indicator register
                u32             wh      : 3;    ///< reserved
                u32             resr    : 24;   ///< offset (in cmbsz units)
            };
        } nvme_power_mgmt_t;

            nvme_power_mgmt_t ps = {.val = res.cs};        
            std::cout << "POWER STATE " << ps.ps << " WH " << ps.wh << std::endl;  */

        // }

        // put the current nvme dirver in the linked list
        _next_nvme_driver = driver::prev_nvme_driver; 
        driver::prev_nvme_driver = this; 
    }

    bool driver::reset_and_destroy_controller() {
        // what we want to do here is reset the controller to a sane state and then disable it in the end

        // 1. remove all open queues on the controller
        remove_all_io_user_queues(); 

        // 2. disable the controller
        shutdown_controller();

        // 3. check for other stuff idky
        usleep(1000000); 

        return true; 
    }

    int driver::set_number_of_queues(u16 num, u16 *ret)
    {
        nvme_sq_entry_t cmd;
        setup_features_cmd(&cmd, NVME_FEATURE_NUM_QUEUES, (num << 16) | num);
        auto res = _admin_queue->submit_and_return_on_completion(&cmd);

        u16 cq_num = res.cs >> 16;
        u16 sq_num = res.cs & 0xffff;

        nvme_d("Queues supported: CQ num=%d, SQ num=%d, MSI/X entries=%d",
               res.cs >> 16, res.cs & 0xffff, _dev.msix_get_num_entries());

        if (res.sct != 0 || res.sc != 0)
            return EIO;

        if (num > cq_num || num > sq_num)
        {
            *ret = (cq_num > sq_num) ? cq_num : sq_num;
        }
        else
        {
            *ret = num;
        }
        return 0;
    }

    int driver::set_interrupt_coalescing(u8 threshold, u8 time)
    {
        nvme_sq_entry_t cmd;
        setup_features_cmd(&cmd, NVME_FEATURE_INT_COALESCING, threshold | (time << 8));
        auto res = _admin_queue->submit_and_return_on_completion(&cmd);

        if (res.sct != 0 || res.sc != 0)
        {
            nvme_e("Failed to enable interrupt coalescing: sc=%#x sct=%#x", res.sc, res.sct);
            return EIO;
        }
        else
        {
            nvme_i("Enabled interrupt coalescing");
            return 0;
        }
    }

    void driver::enable_write_cache()
    {
        nvme_sq_entry_t cmd;
        setup_features_cmd(&cmd, NVME_FEATURE_WRITE_CACHE, 1);
        auto res = _admin_queue->submit_and_return_on_completion(&cmd);
        if (res.sct != 0 || res.sc != 0)
        {
            nvme_e("Failed to enable write cache: sc=%#x sct=%#x", res.sc, res.sct);
        }
        else
        {
            nvme_i("Enabled write cache");
        }
    }

    void driver::create_io_queues()
    {
        u16 ret;
        if (NVME_QUEUE_PER_CPU_ENABLED) {
            set_number_of_queues(sched::cpus.size(), &ret);
        } else {
            set_number_of_queues(1, &ret);
        } 
        assert(ret >= 1);
    
        int qsize = (NVME_IO_QUEUE_SIZE < _control_reg->cap.mqes) ? NVME_IO_QUEUE_SIZE : _control_reg->cap.mqes + 1;
        if (NVME_QUEUE_PER_CPU_ENABLED) {
            for(sched::cpu* cpu : sched::cpus) {
                int qid = cpu->id + 1;
                create_io_queue(qid, qsize, cpu);
            }
        } else {
            create_io_queue(1, qsize);
        }
    }

    enum NVME_CONTROLLER_EN
    {
        CTRL_EN_DISABLE = 0,
        CTRL_EN_ENABLE = 1,
    };

    bool driver::shutdown_controller() {
        nvme_controller_config_t cc;
        cc.val = mmio_getl(&_control_reg->cc);

        assert(cc.en == 1); // 1. If the controller is enabled (i.e., CC.EN is set to ‘1’)

        cc.shn = 1; // normal shutdown
        mmio_setl(&_control_reg->cc, cc.val);
        return wait_for_controller_shutdown_done();
    }

    int driver::enable_disable_controller(bool enable)
    {
        nvme_controller_config_t cc;
        cc.val = mmio_getl(&_control_reg->cc);

        u32 expected_en = enable ? CTRL_EN_DISABLE : CTRL_EN_ENABLE;
        u32 new_en = enable ? CTRL_EN_ENABLE : CTRL_EN_DISABLE;

        if (cc.en == new_en) return 0; 

        assert(cc.en == expected_en); // check current status
        cc.en = new_en;

        mmio_setl(&_control_reg->cc, cc.val);
        return wait_for_controller_ready_change(new_en);
    }

    int driver::get_worst_cast_time() {
        // field is in 500ms units (-> FFh = 127.5s)
        if (_control_reg->cap.to > 0) {
            return _control_reg->cap.to; 
        }
        if (_control_reg->cc.crime == 0) {
            return _control_reg->crto.crwmt; 
        } else {
            return _control_reg->crto.crimt; 
        }
    }

    int driver::wait_for_controller_shutdown_done()
    {
        int timeout = driver::get_worst_cast_time(); // timeout in 0.05ms steps
        nvme_controller_status_t csts;
        for (int i = 0; i < timeout; i++)
        {
            csts.val = mmio_getl(&_control_reg->csts);
            if (csts.shst == 2 && csts.st == 0) {
                printf("controller shutdown properly\n");                 
                return 0;
            }
            usleep(500 * 1000); // steps are in 500ms units
        }
        NVME_ERROR("timeout=%d waiting for shutdown with current status%d type%d", timeout, csts.shst, csts.st);
        return ETIME;
    }

    int driver::wait_for_controller_ready_change(int ready)
    {
        int timeout = driver::get_worst_cast_time(); // timeout in 0.05ms steps
        nvme_controller_status_t csts;
        for (int i = 0; i < timeout; i++)
        {
            csts.val = mmio_getl(&_control_reg->csts);
            if (csts.rdy == ready)
                return 0;
            usleep(500 * 1000); // steps are in 500ms units
        }
        NVME_ERROR("timeout=%d waiting for ready %d", timeout, ready);
        return ETIME;
    }

#define NVME_CTRL_CONFIG_IO_CQ_ENTRY_SIZE_16_BYTES 4
#define NVME_CTRL_CONFIG_IO_SQ_ENTRY_SIZE_64_BYTES 6
#define NVME_CTRL_CONFIG_PAGE_SIZE_4K 0

    void driver::init_controller_config()
    {
        nvme_controller_config_t cc;
        cc.val = mmio_getl(&_control_reg->cc.val);
        cc.iocqes = NVME_CTRL_CONFIG_IO_CQ_ENTRY_SIZE_16_BYTES;
        cc.iosqes = NVME_CTRL_CONFIG_IO_SQ_ENTRY_SIZE_64_BYTES;
        cc.mps = NVME_CTRL_CONFIG_PAGE_SIZE_4K;

        mmio_setl(&_control_reg->cc, cc.val);
    }

    void driver::create_admin_queue()
    {
        u32 *sq_doorbell = _control_reg->sq0tdbl;
        u32 *cq_doorbell = (u32 *)((u64)sq_doorbell + _doorbell_stride);

        int qsize = NVME_ADMIN_QUEUE_SIZE;
        _admin_queue = std::unique_ptr<admin_queue_pair, aligned_new_deleter<admin_queue_pair>>(
            aligned_new<admin_queue_pair>(_id, 0, qsize, _dev, sq_doorbell, cq_doorbell, _ns_data));

        register_admin_interrupt();

        nvme_adminq_attr_t aqa;
        aqa.val = 0;
        aqa.asqs = aqa.acqs = qsize - 1;

        mmio_setl(&_control_reg->aqa, aqa.val);
        mmio_setq(&_control_reg->asq, _admin_queue->sq_phys_addr());
        mmio_setq(&_control_reg->acq, _admin_queue->cq_phys_addr());
    }

    template <typename Q>
    void setup_create_io_queue_cmd(Q *create_queue_cmd, int qid, int qsize, u8 command_opcode, u64 queue_addr)
    {
        assert(create_queue_cmd);
        memset(create_queue_cmd, 0, sizeof(*create_queue_cmd));

        create_queue_cmd->common.opc = command_opcode;
        create_queue_cmd->common.prp1 = queue_addr;
        create_queue_cmd->qid = qid;
        create_queue_cmd->qsize = qsize - 1;
        create_queue_cmd->pc = 1;
    }

    template <typename Q>
    void setup_delete_io_queue_cmd(Q *delete_queue_cmd, int qid, u8 command_opcode, u64 queue_addr)
    {
        assert(delete_queue_cmd);
        memset(delete_queue_cmd, 0, sizeof(*delete_queue_cmd));

        delete_queue_cmd->common.opc = command_opcode;
        delete_queue_cmd->common.prp1 = queue_addr;
        delete_queue_cmd->qid = qid;
    }

    int driver::create_io_queue(int qid, int qsize, sched::cpu* cpu, int qprio)
    {
#ifndef USE_USER_IO_QUEUES

        int iv = qid;
    
        u32* sq_doorbell = (u32*) ((u64) _control_reg->sq0tdbl + 2 * _doorbell_stride * qid);
        u32* cq_doorbell = (u32*) ((u64) sq_doorbell + _doorbell_stride);
    
        // create queue pair with allocated SQ and CQ ring buffers
        auto queue = std::unique_ptr<io_queue_pair, aligned_new_deleter<io_queue_pair>>(
            aligned_new<io_queue_pair>(_id, iv, qsize, _dev, sq_doorbell, cq_doorbell, _ns_data));
    
        // create completion queue command
        nvme_acmd_create_cq_t cmd_cq;
        setup_create_io_queue_cmd<nvme_acmd_create_cq_t>(
            &cmd_cq, qid, qsize, NVME_ACMD_CREATE_CQ, queue->cq_phys_addr());

#ifdef USE_INTERRUPT
        cmd_cq.iv = iv;
        cmd_cq.ien = 1;
#else
        cmd_cq.iv = 0;
        cmd_cq.ien = 0;
#endif

        // create submission queue command
        nvme_acmd_create_sq_t cmd_sq;
        setup_create_io_queue_cmd<nvme_acmd_create_sq_t>(
            &cmd_sq, qid, qsize, NVME_ACMD_CREATE_SQ, queue->sq_phys_addr());
    
        cmd_sq.qprio = qprio;
        cmd_sq.cqid = qid;
    
        _io_queues.push_back(std::move(queue));
#ifdef USE_INTERRUPT
        register_io_interrupt(iv, qid - 1, cpu);
#else
        setup_io_wo_interrupt(qid - 1, cpu); 
#endif

        //According to the NVMe spec, the completion queue (CQ) needs to be created before the submission queue (SQ)
        _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t*)&cmd_cq);
        _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t*)&cmd_sq);
    
        // printf("nvme: Created I/O queue pair for qid:%d with size:%d\n", qid, qsize);
#endif
    
        return 0;
    }

void *driver::create_io_interrupt_user_queue(int individual_qsize) {
        if (individual_qsize <= 0) {
            individual_qsize = _qsize; 
        }
        // TODO: hack search for first qid that is not used by now
        size_t qid = _io_queues.size() + 1;

        // assert(_user_io_queues.find(qid) == _user_io_queues.end());
        assert(qid < (1 << 16));

        u32 *sq_doorbell = (u32 *)((u64)_control_reg->sq0tdbl + 2 * _doorbell_stride * qid);
        u32 *cq_doorbell = (u32 *)((u64)sq_doorbell + _doorbell_stride);

        // create queue pair with allocated SQ and CQ ring buffers
  auto queue = std::unique_ptr<io_user_queue_pair,
                               aligned_new_deleter<io_user_queue_pair>>(
      aligned_new<io_user_queue_pair>(_id, qid, individual_qsize, _dev,
                                      sq_doorbell, cq_doorbell, _ns_data));

  // create completion queue command
  nvme_acmd_create_cq_t cmd_cq;
  setup_create_io_queue_cmd<nvme_acmd_create_cq_t>(
      &cmd_cq, qid, individual_qsize, NVME_ACMD_CREATE_CQ,
      queue->cq_phys_addr());

  int iv = qid;

  cmd_cq.iv = iv;
  cmd_cq.ien = 1;

  // create submission queue command
  nvme_acmd_create_sq_t cmd_sq;
  setup_create_io_queue_cmd<nvme_acmd_create_sq_t>(
      &cmd_sq, qid, individual_qsize, NVME_ACMD_CREATE_SQ,
      queue->sq_phys_addr());

  cmd_sq.qprio =
      NVME_IO_QUEUE_PRIORITY_URGENT; // dont know why it is labeled as interrupt
                                     // enabled in nvme struct -> only prio
  cmd_sq.cqid = qid;

  _io_queues.push_back(std::move(queue));

  // TODO: here we probably need to hardcode the placement of the poller threads
  // in the future otherwise they will get put on the same cpu
  register_io_interrupt(iv, qid, sched::current_cpu);

  set_interrupt_coalescing(16, 1); // 200us coalescing or >=8 completed
  // entries According to the NVMe spec, the completion queue (CQ) needs to be
  // created before the submission queue (SQ)
  _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t *)&cmd_cq);
  _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t *)&cmd_sq);

  printf(
      "nvme: Created I/O user queue pair for qid:%d with size:%d pointer=%p\n",
      qid, individual_qsize, _io_queues[qid].get());
  return _io_queues[qid - 1].get();
}

void *driver::create_io_user_queue(int individual_qsize) {
  if (individual_qsize <= 0) {
    individual_qsize = _qsize;
  }
  // TODO: hack search for first qid that is not used by now
  size_t qid = _io_queues.size() + 1;

  // assert(_user_io_queues.find(qid) == _user_io_queues.end());
  assert(qid < (1 << 16));

  u32 *sq_doorbell =
      (u32 *)((u64)_control_reg->sq0tdbl + 2 * _doorbell_stride * qid);
  u32 *cq_doorbell = (u32 *)((u64)sq_doorbell + _doorbell_stride);

  // create queue pair with allocated SQ and CQ ring buffers
  auto queue = std::unique_ptr<io_user_queue_pair,
                               aligned_new_deleter<io_user_queue_pair>>(
      aligned_new<io_user_queue_pair>(_id, qid, individual_qsize, _dev,
                                      sq_doorbell, cq_doorbell, _ns_data));

        // create completion queue command
        nvme_acmd_create_cq_t cmd_cq;
        setup_create_io_queue_cmd<nvme_acmd_create_cq_t>(
            &cmd_cq, qid, individual_qsize, NVME_ACMD_CREATE_CQ, queue->cq_phys_addr());

#ifdef USE_INTERRUPT
        int iv = qid;

        cmd_cq.iv = iv;
        cmd_cq.ien = 1;
#else
        cmd_cq.iv = 0;
        cmd_cq.ien = 0;
#endif
        // create submission queue command
        nvme_acmd_create_sq_t cmd_sq;
        setup_create_io_queue_cmd<nvme_acmd_create_sq_t>(
      &cmd_sq, qid, individual_qsize, NVME_ACMD_CREATE_SQ,
      queue->sq_phys_addr());

  cmd_sq.qprio =
      NVME_IO_QUEUE_PRIORITY_URGENT; // dont know why it is labeled as interrupt
                                     // enabled in nvme struct -> only prio
        cmd_sq.cqid = qid;

        _io_queues.push_back(std::move(queue));

// TODO: here we probably need to hardcode the placement of the poller threads
// in the future otherwise they will get put on the same cpu
#ifdef USE_INTERRUPT
        register_io_interrupt(iv, qid, sched::current_cpu);
#else
#ifdef USE_POLLING_THREAD
        setup_io_wo_interrupt(qid, sched::current_cpu); 
#endif
#endif

  // According to the NVMe spec, the completion queue (CQ) needs to be created
  // before the submission queue (SQ)
        _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t *)&cmd_cq);
        _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t *)&cmd_sq);

  printf(
      "nvme: Created I/O user queue pair for qid:%d with size:%d pointer=%p\n",
      qid, individual_qsize, _io_queues[qid].get());
        return _io_queues[qid - 1].get();
    }

    int driver::remove_io_user_queue(void* queue)
    {
        io_user_queue_pair* io_queue = (io_user_queue_pair*) queue; 
        u32 qid = io_queue->_id;
        
        printf("remove queue with id %d\n", qid); 

        if (_io_queues.size() > qid)
        {
            NVME_ERROR("Remove io user queue failed size=%d, id=%d", _io_queues.size(), qid);
            return 0;
        }

        // create completion queue command
        nvme_acmd_delete_ioq_t cmd_cq;
        setup_delete_io_queue_cmd<nvme_acmd_delete_ioq_t>(
      &cmd_cq, qid, NVME_ACMD_DELETE_CQ, _io_queues[qid - 1]->cq_phys_addr());

        // create submission queue command
        nvme_acmd_delete_ioq_t cmd_sq;
        setup_delete_io_queue_cmd<nvme_acmd_delete_ioq_t>(
      &cmd_sq, qid, NVME_ACMD_DELETE_SQ, _io_queues[qid - 1]->sq_phys_addr());

        // According to the NVMe spec, the completion queue (CQ) needs to be removed before the submission queue (SQ)
        _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t *)&cmd_cq);
        _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t *)&cmd_sq);


        // asm volatile("" : : : "memory"); 
        // _io_queues[qid].reset(); 

        debugf("nvme: Removed I/O user queue pair for qid:%d with size:%d\n", qid, _qsize);

        return 1;
    }

    int driver::remove_all_io_user_queues() {
        int removed = 0;
        for (auto& io_queue_ptr: _io_queues) {
            if (io_queue_ptr) {
                // remove_io_user_queue(io_queue_ptr.get()); 
                removed++; 
            }
        }
        return removed; 
    }

    void driver::create_io_user_queue_endpoints()
    {
    }
    
    int driver::identify_controller()
    {
        assert(_admin_queue);
        nvme_sq_entry_t cmd;
        setup_identify_cmd(&cmd, 0, CMD_IDENTIFY_CONTROLLER);
        auto data = new nvme_identify_ctlr_t;
        auto res = _admin_queue->submit_and_return_on_completion(&cmd, (void *)mmu::virt_to_phys(data), mmu::page_size);

        if (res.sc != 0 || res.sct != 0)
        {
            NVME_ERROR("Identify controller failed nvme%d, sct=%d, sc=%d", _id, res.sct, res.sc);
            return EIO;
        }

        std::cout << "cqes min: " << (u64) (data->cqes >> 4) << "cqes max: " << (u64) (data->cqes & 0xf) << std::endl; 

        _identify_controller.reset(data);
        return 0;
    }

    int driver::identify_namespace(u32 nsid)
    {
        assert(_admin_queue);
        nvme_sq_entry_t cmd;
        setup_identify_cmd(&cmd, nsid, CMD_IDENTIFY_NAMESPACE);
        auto data = std::unique_ptr<nvme_identify_ns_t>(new nvme_identify_ns_t);
        auto res = _admin_queue->submit_and_return_on_completion(&cmd, (void *)mmu::virt_to_phys(data.get()), mmu::page_size);
        if (res.sc != 0 || res.sct != 0)
        {
            NVME_ERROR("Identify namespace failed nvme%d nsid=%d, sct=%d, sc=%d", _id, nsid, res.sct, res.sc);
            return EIO;
        }

        _ns_data.insert(std::make_pair(nsid, new nvme_ns_t));
        _ns_data[nsid]->blockcount = data->ncap;
        _ns_data[nsid]->blockshift = data->lbaf[data->flbas & 0xF].lbads;
        _ns_data[nsid]->blocksize = 1 << _ns_data[nsid]->blockshift;
        _ns_data[nsid]->bpshift = NVME_PAGESHIFT - _ns_data[nsid]->blockshift;
        _ns_data[nsid]->id = nsid;

        printf("Identified namespace with nsid=%d, blockcount=%d, blocksize=%d",
               nsid, _ns_data[nsid]->blockcount, _ns_data[nsid]->blocksize);
        return 0;
    }

    int driver::make_request(bio *bio, u32 nsid)
    {
        #ifndef USE_USER_IO_QUEUES
        if (bio->bio_bcount % _ns_data[nsid]->blocksize || bio->bio_offset % _ns_data[nsid]->blocksize)
        {
            NVME_ERROR("bio request not block-aligned length=%d, offset=%d blocksize=%d\n", bio->bio_bcount, bio->bio_offset, _ns_data[nsid]->blocksize);
            return EINVAL;
        }
        bio->bio_offset = bio->bio_offset >> _ns_data[nsid]->blockshift;
        bio->bio_bcount = bio->bio_bcount >> _ns_data[nsid]->blockshift;


        if ((bio->bio_offset + bio->bio_bcount) >_ns_data[nsid]->blockcount)
        {
            printf("bio request argument error=%d, offset=%d blocksize=%d\n", bio->bio_bcount, bio->bio_offset, _ns_data[nsid]->blockcount); 
            NVME_ERROR("bio request argument error=%d, offset=%d blocksize=%d\n", bio->bio_bcount, bio->bio_offset, _ns_data[nsid]->blockcount);
            return EINVAL;
        }
        assert((bio->bio_offset + bio->bio_bcount) <= _ns_data[nsid]->blockcount);

        if (bio->bio_cmd == BIO_FLUSH && (_identify_controller->vwc == 0 || !NVME_VWC_ENABLED))
        {
            biodone(bio, true);
            return 0;
        }

        unsigned int qidx = sched::current_cpu->id % _io_queues.size();

        return _io_queues[qidx]->make_request(bio, nsid);
        #else
        return 0; 
        #endif
    }

    void driver::register_admin_interrupt()
    {
        sched::thread *aq_thread = sched::thread::make([this]
                                                       { this->_admin_queue->req_done(); },
                                                       sched::thread::attr().name("nvme" + std::to_string(_id) + "_aq_req_done"));
        aq_thread->start();

        assert(msix_register(0, [this]
                             { this->_admin_queue->disable_interrupts(); }, aq_thread));
    }

    void driver::enable_msix()
    {
        _dev.set_bus_master(true);
        _dev.msix_enable();
        assert(_dev.is_msix());
    
        unsigned int vectors_num = 16; //at least for admin

        // TODO: this is so hacky and you should just add new ptrs on demand
        // and not push them; currently i am not sure if this can be done mt so 
        // i'll go with that solution to just make it big enough
        _msix_vectors = std::vector<std::unique_ptr<msix_vector>>(vectors_num);
    }

bool driver::msix_register_io_queue(unsigned iv, unsigned qid) {
  _dev.msix_mask_all();
  _dev.msix_mask_entry(iv);

  auto vec = std::unique_ptr<msix_vector>(new msix_vector(&_dev));
  printf("msix vector: %i\n", vec->get_vector()); 
  _msi.assign_isr(vec.get(), [=]() mutable {
    this->_io_queues[qid - 1]->process_completions(512);
  }); // this basically sets the handler of the vec to the function given

  if (!_msi.setup_entry(iv, vec.get())) {
    return false;
  }

  vec->set_affinity(sched::current_cpu->arch.apic_id);

  if (iv < _msix_vectors.size()) {
    _msix_vectors[iv] = std::move(vec);
  } else {
    NVME_ERROR("binding_entry %d registration failed\n", iv);
    return false;
  }
  _msix_vectors[iv]->msix_unmask_entries();

  _dev.msix_unmask_all();
  _dev.msix_unmask_entry(iv);
  return true;
}
    
    bool driver::msix_register(unsigned iv,
        // high priority ISR
        std::function<void ()> isr,
        // bottom half
        sched::thread *t,
        bool assign_affinity)
    {
        //Mask all interrupts...
        _dev.msix_mask_all();
        _dev.msix_mask_entry(iv);
    
        auto vec = std::unique_ptr<msix_vector>(new msix_vector(&_dev));
        _msi.assign_isr(vec.get(),
            [=]() mutable {
                      isr();
                      t->wake_with_irq_disabled();
                  }); // this basically sets the handler of the vec to the function given 
    
        if (!_msi.setup_entry(iv, vec.get())) {
            return false;
        }
    
        if (assign_affinity && t) {
            vec->set_affinity(t->get_cpu()->arch.apic_id);
        }
    
        if (iv < _msix_vectors.size()) {
            _msix_vectors[iv] = std::move(vec);
        } else {
            NVME_ERROR("binding_entry %d registration failed\n",iv);
            return false;
        }
        _msix_vectors[iv]->msix_unmask_entries();
    
        _dev.msix_unmask_all();
        _dev.msix_unmask_entry(iv);
        return true;
    }
    
// qid should be the index that corresponds to the queue in _io_queues.
// In general qid = iv - 1
bool driver::register_io_interrupt(unsigned int iv, unsigned int qid,
                                   sched::cpu *cpu) {
  if (_io_queues[qid - 1]->_id != iv)
    abort();
  bool ok = msix_register_io_queue(iv, qid);
  // sched::thread *t;
  /* bool ok;

  if (_io_queues.size() <= qid) {
            NVME_ERROR("queue %d not initialized\n",qid);
            return false;
  }


  //    nvme_w("Queue %d ->_id = %d != iv %d\n", qid, _io_queues[qid- 1]->_id,
  //    iv);
    
  t = sched::thread::make(
      [this, qid]
      { this->_io_queues[qid - 1]->req_done(); },
      sched::thread::attr().name("nvme" + std::to_string(_id) + "_ioq" +
                                 std::to_string(qid) + "_iv" +
                                 std::to_string(iv)));
        t->start();
    
        // If cpu specified, let us pin the worker thread to this cpu
        bool pin = cpu != nullptr;
  if (pin)
  {
            sched::thread::pin(t, cpu);
        }
    
  ok = msix_register(
      iv, [this, qid]
      { this->_io_queues[qid - 1]->disable_interrupts(); }, t,
      true); */
        if (not ok)
    printf("Interrupt registration failed: queue=%d interruptvector=%d\n", qid,
           iv);
  else {
    printf("Interrrupt registration successful: queue=%d interruptvector=%d\n",
           qid, iv);
  }
        return ok;
    }

    void driver::setup_io_wo_interrupt(unsigned int qid, sched::cpu* cpu) {
        sched::thread* t;
    
        // assert(_io_queues.size() > qid);
        

        t = sched::thread::make([this,qid] { 
            sched::thread::pin(sched::cpus[4]); 
            printf("nvme: IO poller for qid:%d\n", qid);
            this->_io_queues[qid- 1]->req_done(); 
        });
        t->start();
    
        // If cpu specified, let us pin the worker thread to this cpu
        bool pin = cpu != nullptr;
        if (pin) {
            sched::thread::pin(t, cpu);
        }
    }
    

    void driver::dump_config(void)
    {
        u8 B, D, F;
        _dev.get_bdf(B, D, F);

        _dev.dump_config();
        printf("%s [%x:%x.%x] vid:id= %x:%x\n", get_name().c_str(),
               (u16)B, (u16)D, (u16)F,
               _dev.get_vendor_id(),
               _dev.get_device_id());
    }

    bool driver::parse_pci_config()
    {
        _bar0 = _dev.get_bar(1);
        if (_bar0 == nullptr)
        {
            return false;
        }
        _bar0->map();
        if (!_bar0->is_mapped())
        {
            return false;
        }
        _control_reg = (nvme_controller_reg_t *)_bar0->get_mmio();
        return true;
    }

    hw_driver *driver::probe(hw_device *dev)
    {
        if (auto pci_dev = dynamic_cast<pci::device *>(dev))
        {
            if ((pci_dev->get_base_class_code() == pci::function::PCI_CLASS_STORAGE) &&
                (pci_dev->get_sub_class_code() == pci::function::PCI_SUB_CLASS_STORAGE_NVMC) &&
                (pci_dev->get_programming_interface() == 2)) // detect NVMe device
                return aligned_new<driver>(*pci_dev);
        }
        return nullptr;
    }

    driver* driver::get_nvme_device(int id) {
        driver* current_driver = driver::prev_nvme_driver; 

        while (current_driver != nullptr) {
            if (current_driver->_id == id) {
                return current_driver; 
            }
            current_driver = current_driver->_next_nvme_driver; 
        }

        return nullptr; 
    }
}
