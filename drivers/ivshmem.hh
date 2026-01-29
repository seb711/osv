/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef IVSH_DRIVER_H
#define IVSH_DRIVER_H

#include "driver.hh"
#include "drivers/driver.hh"

#include <osv/aligned_new.hh>

namespace ivshmem
{
    
    const unsigned IVSH_VENDOR_ID = 0x1af4;
    
    class ivshmem_driver : public hw_driver
    {
        public:
        explicit ivshmem_driver(pci::device &dev);
        virtual ~ivshmem_driver() {};
        
        static hw_driver *probe(hw_device *dev);
        
        virtual std::string get_name() const { return "ivshmem"; };
        
        virtual void dump_config();
        
        bool parse_pci_config();
        
        volatile void* get_shared_mem() { return _bar0->get_mmio(); }; 
        
        protected:
        pci::device &_dev;
        pci::bar *_bar0 = nullptr;
    };
    
    extern "C" ivshmem_driver* ivsh_dev; 
}

#endif
