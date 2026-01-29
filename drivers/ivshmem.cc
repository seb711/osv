#include "ivshmem.hh"

#include <iostream>

namespace ivshmem
{
    ivshmem_driver* ivsh_dev = nullptr; 

    ivshmem_driver::ivshmem_driver(pci::device &pci_dev) : _dev(pci_dev)
    {
        auto parse_ok = parse_pci_config();
        assert(parse_ok);
        std::cout << "init ivsh driver" << std::endl;

        ivsh_dev = this; 
    }

    void ivshmem_driver::dump_config(void)
    {
        u8 B, D, F;
        _dev.get_bdf(B, D, F);

        _dev.dump_config();
        printf("%s [%x:%x.%x] vid:id= %x:%x\n", get_name().c_str(),
               (u16)B, (u16)D, (u16)F,
               _dev.get_vendor_id(),
               _dev.get_device_id());
    }

    bool ivshmem_driver::parse_pci_config()
    {
        _bar0 = _dev.get_bar(3);
        if (_bar0 == nullptr)
        {
            std::cout << "mapping failed" << std::endl; 
            return false;
        }
        _bar0->map();
        if (!_bar0->is_mapped())
        {
            std::cout << "mapping failed 2" << std::endl; 
            return false;
        }

        volatile void* t = _bar0->get_mmio(); 

        std::cout << "in here: " << std::hex << ((int*) t)[0] << std::endl; 

        // something should be done here

        return true;
    }

    hw_driver *ivshmem_driver::probe(hw_device *dev)
    {
        if (auto pci_dev = dynamic_cast<pci::device *>(dev))
        {
            // Check for ivshmem device (vendor 0x1AF4, device 0x1110)
            if (pci_dev->get_vendor_id() == 0x1AF4 &&
                pci_dev->get_device_id() == 0x1110)
                return aligned_new<ivshmem_driver>(*pci_dev);
        }
        return nullptr;
    }
}