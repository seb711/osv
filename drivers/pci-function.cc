/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/mmio.hh>
#include <osv/pci.hh>
#include "device.hh"
#include "pci-function.hh"

using namespace hw;

namespace pci {

    bar::bar(function* dev, u8 pos)
        : _dev(dev), _pos(pos),
          _addr_lo(0), _addr_hi(0), _addr_64(0), _addr_size(0),
          _addr_mmio(mmio_nullptr),
          _is_mmio(false), _is_64(false), _is_prefetchable(false)
    {
        u32 val = _dev->pci_readl(_pos);

        _is_mmio = ((val & PCI_BAR_MEMORY_INDICATOR_MASK) == PCI_BAR_MMIO);
        if (_is_mmio) {
            _is_64 = ((val & PCI_BAR_MEM_ADDR_SPACE_MASK)
                == PCI_BAR_64BIT_ADDRESS);
            _is_prefetchable = ((val & PCI_BAR_PREFETCHABLE_MASK)
                == PCI_BAR_PREFETCHABLE);
        }
        _addr_size = read_bar_size();

        val = pci::bar::arch_add_bar(val);

        if (_is_mmio) {
            _addr_lo = val & PCI_BAR_MEM_ADDR_LO_MASK;
            if (_is_64) {
                _addr_hi = _dev->pci_readl(_pos + 4);
            }
        } else {
            _addr_lo = val & PCI_BAR_PIO_ADDR_MASK;
        }

        _addr_64 = ((u64)_addr_hi << 32) | (u64)(_addr_lo);
    }

    bar::~bar()
    {

    }

    u64 bar::read_bar_size()
    {
        // The device must not decode the following BAR values since they aren't
        // addresses
        _dev->disable_bars_decode(true, true);

        u32 lo_orig = _dev->pci_readl(_pos);

        // Size test
        _dev->pci_writel(_pos, 0xFFFFFFFF);
        u32 lo = _dev->pci_readl(_pos);
        // Restore
        _dev->pci_writel(_pos, lo_orig);

        if (is_pio()) {
            lo &= PCI_BAR_PIO_ADDR_MASK;
        } else {
            lo &= PCI_BAR_MEM_ADDR_LO_MASK;
        }

        u32 hi = 0xFFFFFFFF;

        if (is_64()) {
            u32 hi_orig = _dev->pci_readl(_pos+4);
            _dev->pci_writel(_pos+4, 0xFFFFFFFF);
            hi = _dev->pci_readl(_pos+4);
            // Restore
            _dev->pci_writel(_pos+4, hi_orig);
        }

        _dev->enable_bars_decode(true, true);

        u64 bits = (u64)hi << 32 | lo;
        return ~bits + 1;
    }

    void bar::map()
    {
        if (_is_mmio) {
            _addr_mmio = mmio_map(get_addr64(), get_size(), "pci_bar");
        }
    }

    void bar::unmap()
    {
        if ((_is_mmio) && (_addr_mmio != mmio_nullptr)) {
            mmio_unmap(_addr_mmio, get_size());
        }
    }

    bool bar::is_mapped()
    {
        return _addr_mmio != mmio_nullptr;
    }

    mmioaddr_t bar::get_mmio()
    {
        return _addr_mmio;
    }

    u64 bar::readq(u64 offset)
    {
        if (_is_mmio) {
            return mmio_getq(_addr_mmio + offset);
        } else {
            abort("64 bit read attempt from PIO area");
        }
    }

    u32 bar::readl(u64 offset)
    {
        if (_is_mmio) {
            return mmio_getl(_addr_mmio + offset);
        } else {
            return inl(_addr_lo + offset);
        }
    }

    u16 bar::readw(u64 offset)
    {
        if (_is_mmio) {
            return mmio_getw(_addr_mmio + offset);
        } else {
            return inw(_addr_lo + offset);
        }
    }

    u8 bar::readb(u64 offset)
    {
        if (_is_mmio) {
            return mmio_getb(_addr_mmio + offset);
        } else {
            return inb(_addr_lo + offset);
        }
    }

    void bar::writeq(u64 offset, u64 val)
    {
        if (_is_mmio) {
            mmio_setq(_addr_mmio + offset, val);
        } else {
            abort("64 bit write attempt to PIO area");
        }
    }

    void bar::writel(u64 offset, u32 val)
    {
        if (_is_mmio) {
            mmio_setl(_addr_mmio + offset, val);
        } else {
            outl(val, _addr_lo + offset);
        }
    }

    void bar::writew(u64 offset, u16 val)
    {
        if (_is_mmio) {
            mmio_setw(_addr_mmio + offset, val);
        } else {
            outw(val, _addr_lo + offset);
        }
    }

    void bar::writeb(u64 offset, u8 val)
    {
        if (_is_mmio) {
            mmio_setb(_addr_mmio + offset, val);
        } else {
            outb(val, _addr_lo + offset);
        }
    }

    function::function(u8 bus, u8 device, u8 func)
        : _bus(bus), _device(device), _func(func), _have_msix(false), _msix_enabled(false), _have_msi(false), _msi_enabled(false)
    {

    }

    function::~function()
    {
        for (auto it = _bars.begin(); it != _bars.end(); it++) {
            delete (it->second);
        }
    }

    hw_device_id function::get_id()
    {
        return hw_device_id(_vendor_id, _device_id);
    }

    void function::print()
    {
        dump_config();
    }

    void function::reset()
    {
        // TODO: implement
    }

     // Define a struct for the PCI_EXP_LNKSTA bit fields
    typedef union _pcie_exp_link
    {
        uint16_t val;
        struct
        {
            uint16_t current_link_speed : 4;    // Bits [0-3]
            uint16_t negotiated_link_width : 6; // Bits [4-9]
            uint16_t reserved : 1;              // Bit [10] (reserved for alignment)
            uint16_t link_training : 1;         // Bit [11]
            uint16_t slot_clock_config : 1;     // Bit [12]
            uint16_t dll_link_active : 1;       // Bit [13]
            uint16_t bandwidth_mgmt_status : 1; // Bit [14]
            uint16_t auto_bandwidth_status : 1; // Bit [15]
        };
    } pcie_exp_link_t;

    // Define a struct for the PCI_EXP_LNKSTA bit fields
    typedef union _pcie_exp_linkcap
    {
        uint32_t val;
        struct
        {
            uint32_t supported_link_speeds : 4;         // Bits [0-3]
            uint32_t maximum_link_width : 6;            // Bits [4-9]
            uint32_t aspm_support : 2;                  // Bits [10-11]
            uint32_t l0s_exit_latency : 3;              // Bits [12-14]
            uint32_t l1_exit_latency : 3;               // Bits [15-17]
            uint32_t clock_power_management : 1;        // Bit [18]
            uint32_t surprise_down_error_reporting : 1; // Bit [19]
            uint32_t dll_active_reporting : 1;          // Bit [20]
            uint32_t bandwidth_notification : 1;        // Bit [21]
            uint32_t reserved : 2;                      // Bits [22-23] (reserved)
            uint32_t port_number : 8;                   // Bits [24-31]
        };
    } pcie_exp_linkcap_t;

    // Define a struct for the PCI_EXP_LNKSTA bit fields
    typedef union _pcie_exp_linkctl
    {
        uint16_t val;
        struct
        {
            uint16_t aspm_control : 2;             // Bits [0-1]: ASPM Control
            uint16_t reserved_1 : 1;               // Bit [2]: Reserved
            uint16_t read_completion_boundary : 1; // Bit [3]: Read Completion Boundary
            uint16_t link_disable : 1;             // Bit [4]: Link Disable
            uint16_t retrain_link : 1;             // Bit [5]: Retrain Link
            uint16_t common_clock_config : 1;      // Bit [6]: Common Clock Configuration
            uint16_t extended_synch : 1;           // Bit [7]: Extended Synch
            uint16_t clkreq_enable : 1;            // Bit [8]: Enable clkreq
            uint16_t hardware_width_disable : 1;   // Bit [9]: Hardware Autonomous Width Disable
            uint16_t lbm_interrupt_enable : 1;     // Bit [10]: Link Bandwidth Management Interrupt Enable
            uint16_t lab_interrupt_enable : 1;     // Bit [11]: Link Autonomous Bandwidth Interrupt Enable
            uint16_t reserved_2 : 4;               // Bits [12-15]: Reserved
        };
    } pcie_exp_linkctl_t;

    typedef union _pcie_exp_linkctl2 {
            uint16_t val;
            struct
                    {

        uint16_t target_speed : 4; /* Set the target speed */
        uint16_t enter_compliance : 1;
        uint16_t hardware_autonomous_width_disable : 1;
        uint16_t selectable_de_emphasis : 1;
        uint16_t transmit_margin : 3;
        uint16_t enter_modified_compliance : 1;
        uint16_t compliance_sos : 1;
        uint16_t compliance_preset : 4;
    };
    } pcie_exp_linkctl2_t;

    void print_pci_configs(function& pci_func); 

    void pci_retrain(function& pci_func)
    {
        std::vector<u8> cap_offsets;
        if (pci_func.find_capabilities(pci::function::PCI_CAP_EXPRESS, cap_offsets))
        {
            for (auto offset : cap_offsets)
            {  
                u16 link_ctl_val2 = pci_func.pci_readw(offset + 0x30);
                pcie_exp_linkctl2_t lnkctl2; 
                lnkctl2.val = link_ctl_val2;

                lnkctl2.target_speed = 1;

                pci_func.pci_writew(offset + 0x30, lnkctl2.val);


                u16 link_ctl_val = pci_func.pci_readw(offset + 0x10);
                pcie_exp_linkctl_t lnkctl;
                lnkctl.val = link_ctl_val;

                lnkctl.retrain_link = 1;

                pci_func.pci_writew(offset + 0x10, lnkctl.val);
                print_pci_configs(pci_func); 

                pcie_exp_link_t lnksta;

                do {
                    printf("not finished\n"); 
                    u16 link_sta_val = pci_func.pci_readw(offset + 0x12);
                    lnksta.val = link_sta_val;
                } while (lnksta.link_training > 0); 

                printf("finished retraining\n");

                link_ctl_val2 = pci_func.pci_readw(offset + 0x30);
                lnkctl2.val = link_ctl_val2;

                lnkctl2.target_speed = 5;

                pci_func.pci_writew(offset + 0x30, lnkctl2.val);


                link_ctl_val = pci_func.pci_readw(offset + 0x10);
                lnkctl.val = link_ctl_val;

                lnkctl.retrain_link = 1;

                pci_func.pci_writew(offset + 0x10, lnkctl.val);
                print_pci_configs(pci_func); 

                do {
                    printf("not finished\n"); 
                    u16 link_sta_val = pci_func.pci_readw(offset + 0x12);
                    lnksta.val = link_sta_val;
                } while (lnksta.link_training > 0); 

                printf("finished retraining\n");
            }
        }
    }

    void print_pci_configs(function& pci_func)
    {
        std::vector<u8> cap_offsets;
        if (pci_func.find_capabilities(pci::function::PCI_CAP_EXPRESS, cap_offsets))
        {
            for (auto offset : cap_offsets)
            {
                u32 link_cap_val = pci_func.pci_readl(offset + 0x0c);
                pcie_exp_linkcap_t lnkcap;
                lnkcap.val = link_cap_val;

                // Access individual fields
                printf("----------- LINK CAPABILITIES ----------------\n");
                printf("Supported Link Speeds: 0x%X\n", lnkcap.supported_link_speeds);
                printf("Maximum Link Width: 0x%X\n", lnkcap.maximum_link_width);
                printf("ASPM Support: 0x%X\n", lnkcap.aspm_support);
                printf("L0s Exit Latency: 0x%X\n", lnkcap.l0s_exit_latency);
                printf("L1 Exit Latency: 0x%X\n", lnkcap.l1_exit_latency);
                printf("Clock Power Management: %d\n", lnkcap.clock_power_management);
                printf("Surprise Down Error Reporting: %d\n", lnkcap.surprise_down_error_reporting);
                printf("DLL Active Reporting Capable: %d\n", lnkcap.dll_active_reporting);
                printf("Link Bandwidth Notification: %d\n", lnkcap.bandwidth_notification);
                printf("Port Number: 0x%X\n", lnkcap.port_number);
                printf("----------- END LINK CAPABILITIES ----------------\n");
                u16 link_sta_val = pci_func.pci_readw(offset + 0x12);
                pcie_exp_link_t lnksta;
                lnksta.val = link_sta_val;

                // Access individual fields
                printf("----------- LINK STATUS ----------------\n");
                printf("Current Link Speed: 0x%X\n", lnksta.current_link_speed);
                printf("Negotiated Link Width: 0x%X\n", lnksta.negotiated_link_width); // #define  PCI_EXP_LNKSTA_NLW_SHIFT 4	/* start of NLW mask in link status */
                printf("Link Training: %d\n", lnksta.link_training);
                printf("Slot Clock Config: %d\n", lnksta.slot_clock_config);
                printf("DLL Link Active: %d\n", lnksta.dll_link_active);
                printf("Bandwidth Mgmt Status: %d\n", lnksta.bandwidth_mgmt_status);
                printf("Autonomous Bandwidth Status: %d\n", lnksta.auto_bandwidth_status);
                printf("----------- END LINK STATUS ----------------\n");
            }
        }
    }

    bool function::parse_pci_config()
    {
        _device_id = pci_readw(PCI_CFG_DEVICE_ID);
        _vendor_id = pci_readw(PCI_CFG_VENDOR_ID);
        _revision_id = pci_readb(PCI_CFG_REVISION_ID);
        _header_type = pci_readb(PCI_CFG_HEADER_TYPE);
        _base_class_code = pci_readb(PCI_CFG_CLASS_CODE0);
        _sub_class_code = pci_readb(PCI_CFG_CLASS_CODE1);
        _programming_interface = pci_readb(PCI_CFG_CLASS_CODE2);

        printf("device id: %u base class: %u sub class: %u \n", _device_id, _base_class_code, _sub_class_code);

        if (_base_class_code == 0x01 && _sub_class_code == 0x08)
        {
            printf("device id: %u base class: %u sub class: %u \n", _device_id, _base_class_code, _sub_class_code);

            if (_base_class_code == 0x01 && _sub_class_code == 0x08)
            {
                // if nvme then trigger retrain
                print_pci_configs(*this); 
                pci_retrain(*this);
                print_pci_configs(*this); 
            }
        } 

        // Parse capabilities
        bool parse_ok = parse_pci_capabilities();

        return parse_ok;
    }

    bool function::parse_pci_capabilities()
    {
        // Parse MSI-X
        u8 off = find_capability(PCI_CAP_MSIX);
        if (off != 0xFF) {
            bool msi_ok = parse_pci_msix(off);
            return msi_ok;
        }

        // Parse MSI
        off = find_capability(PCI_CAP_MSI);
        if (off != 0xFF) {
            return parse_pci_msi(off);
        }

        return true;
    }

    bool function::parse_pci_msi(u8 off)
    {
        // Location within the configuration space
        _msi.msi_location = off;
        _msi.msi_ctrl = pci_readw(off + PCIR_MSI_CTRL);
        // TODO: support multiple MSI message
        _msi.msi_msgnum = 1;

        if (_msi.msi_ctrl & (1 << 7)) {
            _msi.is_64_address = true;
        } else {
            _msi.is_64_address = false;
        }

        if (_msi.msi_ctrl & (1 << 8)) {
            _msi.is_vector_mask = true;
        } else {
            _msi.is_vector_mask = false;
        }

        // We've found an MSI-x capability
        _have_msi = true;

        return true;
    }

    bool function::parse_pci_msix(u8 off)
    {
        // Used for parsing MSI-x
        u32 val = 0;

        // Location within the configuration space
        _msix.msix_location = off;
        _msix.msix_ctrl = pci_readw(off + PCIR_MSIX_CTRL);
        _msix.msix_msgnum = (_msix.msix_ctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1;
        val = pci_readl(off + PCIR_MSIX_TABLE);
        _msix.msix_table_bar = val & PCIM_MSIX_BIR_MASK;
        _msix.msix_table_offset = val & ~PCIM_MSIX_BIR_MASK;
        val = pci_readl(off + PCIR_MSIX_PBA);
        _msix.msix_pba_bar = val & PCIM_MSIX_BIR_MASK;
        _msix.msix_pba_offset = val & ~PCIM_MSIX_BIR_MASK;

        // We've found an MSI-x capability
        _have_msix = true;

        return true;
    }

    void function::get_bdf(u8& bus, u8 &device, u8& func)
    {
        bus = _bus;
        device = _device;
        func = _func;
    }

    void function::set_bdf(u8 bus, u8 device, u8 func)
    {
        _bus = bus;
        _device = device;
        _func = func;
    }

    u16 function::get_vendor_id()
    {
        return _vendor_id;
    }

    u16 function::get_device_id()
    {
        return _device_id;
    }

    u8 function::get_revision_id()
    {
        return _revision_id;
    }

    u8 function::get_base_class_code()
    {
        return _base_class_code;
    }

    u8 function::get_sub_class_code()
    {
        return _sub_class_code;
    }

    u8 function::get_programming_interface()
    {
        return _programming_interface;
    }

    bool function::is_device()
    {
        return (_header_type & PCI_HDR_TYPE_MASK) == PCI_HDR_TYPE_DEVICE;
    }

    bool function::is_bridge()
    {
        return (_header_type & PCI_HDR_TYPE_MASK) == PCI_HDR_TYPE_BRIDGE;
    }

    bool function::is_pccard()
    {
        return (_header_type & PCI_HDR_TYPE_MASK)== PCI_HDR_TYPE_PCCARD;
    }

    bool function::is_device(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type & PCI_HDR_TYPE_MASK) == PCI_HDR_TYPE_DEVICE;
    }

    bool function::is_bridge(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type & PCI_HDR_TYPE_MASK) == PCI_HDR_TYPE_BRIDGE;
    }

    bool function::is_pccard(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type & PCI_HDR_TYPE_MASK)  == PCI_HDR_TYPE_PCCARD;
    }

    // Command & Status
    u16 function::get_command()
    {
        return pci_readw(PCI_CFG_COMMAND);
    }

    u16 function::get_status()
    {
        return pci_readw(PCI_CFG_STATUS);
    }

    void function::set_command(u16 command)
    {
        pci_writew(PCI_CFG_COMMAND, command);
    }

    void function::set_status(u16 status)
    {
        pci_writew(PCI_CFG_COMMAND, status);
    }

    bool function::get_bus_master()
    {
        u16 command = get_command();
        return command & PCI_COMMAND_BUS_MASTER;
    }

    void function::set_bus_master(bool master)
    {
        u16 command = get_command();
        command =
            (master) ?
                command | PCI_COMMAND_BUS_MASTER :
                command & ~PCI_COMMAND_BUS_MASTER;
        set_command(command);
    }

    void function::enable_bars_decode(bool mem, bool io)
    {
        u16 command = get_command();
        if (mem) {
            command |= PCI_COMMAND_BAR_MEM_ENABLE;
        }
        if (io) {
            command |= PCI_COMMAND_BAR_IO_ENABLE;
        }
        set_command(command);
    }

    void function::disable_bars_decode(bool mem, bool io)
    {
        u16 command = get_command();
        if (mem) {
            command &= ~(u16)PCI_COMMAND_BAR_MEM_ENABLE;
        }
        if (io) {
            command &= ~(u16)PCI_COMMAND_BAR_IO_ENABLE;
        }
        set_command(command);
    }

    bool function::is_intx_enabled()
    {
        u16 command = get_command();
        return (command & PCI_COMMAND_INTX_DISABLE) == 0;
    }

    void function::enable_intx()
    {
        u16 command = get_command();
        command &= ~PCI_COMMAND_INTX_DISABLE;
        set_command(command);
    }

    void function::disable_intx()
    {
        u16 command = get_command();
        command |= PCI_COMMAND_INTX_DISABLE;
        set_command(command);
    }

    u8 function::get_interrupt_line()
    {
        return pci_readb(PCI_CFG_INTERRUPT_LINE);
    }

    void function::set_interrupt_line(u8 irq)
    {
        pci_writeb(PCI_CFG_INTERRUPT_LINE, irq);
    }

    u8 function::get_interrupt_pin()
    {
        return pci_readb(PCI_CFG_INTERRUPT_PIN);
    }

    bool function::is_msix()
    {
        return _have_msix;
    }

    bool function::is_msi()
    {
        return _have_msi;
    }

    unsigned function::msix_get_num_entries()
    {
        if (!is_msix()) {
            return 0;
        }

        return _msix.msix_msgnum;
    }

    unsigned function::msi_get_num_entries()
    {
        if (!is_msi()) {
            return 0;
        }

        return _msi.msi_msgnum;
    }

    void function::msix_mask_all()
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl |= PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);
    }

    void function::msi_mask_all()
    {
        if (!is_msi()) {
            return;
        }
        for (int i = 0; i < _msi.msi_msgnum; i++) {
            msi_mask_entry(i);
        }
    }

    void function::msix_unmask_all()
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl &= ~PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);
    }

    void function::msi_unmask_all()
    {
        if (!is_msi()) {
            return;
        }
        for (int i = 0; i < _msi.msi_msgnum; i++) {
            msi_unmask_entry(i);
        }
    }

    bool function::msix_mask_entry(int entry_id)
    {
        if (!is_msix()) {
            return false;
        }

        if (entry_id >= _msix.msix_msgnum) {
            return false;
        }

        mmioaddr_t entryaddr = msix_get_table() + (entry_id * MSIX_ENTRY_SIZE);
        mmioaddr_t ctrl = entryaddr + (u8)MSIX_ENTRY_CONTROL;

        u32 ctrl_data = mmio_getl(ctrl);
        ctrl_data |= (1 << MSIX_ENTRY_CONTROL_MASK_BIT);
        mmio_setl(ctrl, ctrl_data);

        return true;
    }

    bool function::msi_mask_entry(int entry_id)
    {
        if (!is_msi()) {
            return false;
        }

        if (entry_id >= _msi.msi_msgnum) {
            return false;
        }

        // Per-vector mask enabled?
        if (_msi.is_vector_mask) {
            // 64 bits address enabled?
            if (_msi.is_64_address) {
                auto reg = _msi.msi_location + PCIR_MSI_MASK_64;
                auto mask = pci_readl(reg);
                mask |= 1 << entry_id;
                pci_writel(reg, mask);
            } else {
                auto reg = _msi.msi_location + PCIR_MSI_MASK_32;
                auto mask = pci_readl(reg);
                mask |= 1 << entry_id;
                pci_writel(reg, mask);
            }
        }

        return true;
    }

    bool function::msix_unmask_entry(int entry_id)
    {
        if (!is_msix()) {
            return false;
        }

        if (entry_id >= _msix.msix_msgnum) {
            return false;
        }

        mmioaddr_t entryaddr = msix_get_table() + (entry_id * MSIX_ENTRY_SIZE);
        mmioaddr_t ctrl = entryaddr + (u8)MSIX_ENTRY_CONTROL;

        u32 ctrl_data = mmio_getl(ctrl);
        ctrl_data &= ~(1 << MSIX_ENTRY_CONTROL_MASK_BIT);
        mmio_setl(ctrl, ctrl_data);

        return true;
    }

    bool function::msi_unmask_entry(int entry_id)
    {
        if (!is_msi()) {
            return false;
        }

        if (entry_id >= _msi.msi_msgnum) {
            return false;
        }

        // Per-vector mask enabled?
        if (_msi.is_vector_mask) {
            // 64 bits address enabled?
            if (_msi.is_64_address) {
                auto reg = _msi.msi_location + PCIR_MSI_MASK_64;
                auto mask = pci_readl(reg);
                mask &= ~(1 << entry_id);
                pci_writel(reg, mask);
            } else {
                auto reg = _msi.msi_location + PCIR_MSI_MASK_32;
                auto mask = pci_readl(reg);
                mask &= ~(1 << entry_id);
                pci_writel(reg, mask);
            }
        }

        return true;
    }

    bool function::msix_write_entry(int entry_id, u64 address, u32 data)
    {
        if (!is_msix()) {
            return false;
        }

        if (entry_id >= _msix.msix_msgnum) {
            return false;
        }

        mmioaddr_t entryaddr = msix_get_table() + (entry_id * MSIX_ENTRY_SIZE);

        mmio_setq(entryaddr + (u8)MSIX_ENTRY_ADDR, address);
        mmio_setl(entryaddr + (u8)MSIX_ENTRY_DATA, data);

        return true;
    }

    bool function::msi_write_entry(int entry_id, u64 address, u32 data)
    {
        if (!is_msi()) {
            return false;
        }

        if (entry_id >= _msi.msi_msgnum) {
            return false;
        }

        // 64 Bit message address enabled ?
        if (_msi.is_64_address) {
            pci_writel(_msi.msi_location + PCIR_MSI_ADDR, address & 0xFFFFFFFF);
            pci_writel(_msi.msi_location + PCIR_MSI_UADDR, address >> 32);
            pci_writel(_msi.msi_location + PCIR_MSI_DATA_64, data);
        } else {
            pci_writel(_msi.msi_location + PCIR_MSI_ADDR, address & 0xFFFFFFFF);
            pci_writel(_msi.msi_location + PCIR_MSI_DATA_32, data);
        }

        return true;
    }

    void function::msix_enable()
    {
        if (!is_msix() || _msix_enabled) {
            return;
        }

        // mmap the msix bar into memory
        bar* msix_bar = get_bar(_msix.msix_table_bar + 1);
        if (msix_bar == nullptr) {
            return;
        }

        msix_bar->map();

        // Disabled intx assertions which is turned on by default
        disable_intx();

        // Only after enabling msix, the access to the pci bar is permitted
        // so we enable it while masking all interrupts in the msix ctrl reg
        u16 ctrl = msix_get_control();
        ctrl |= PCIM_MSIXCTRL_MSIX_ENABLE;
        ctrl |= PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);

        // Mask all individual entries
        for (int i=0; i<_msix.msix_msgnum; i++) {
            msix_mask_entry(i);
        }

        // After all individual entries are masked,
        // Unmask the main block
        ctrl &= ~PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);

        _msix_enabled = true;
    }

    void function::msi_enable()
    {
        if (!is_msi() || _msi_enabled) {
            return;
        }

        // Disabled intx assertions which is turned on by default
        disable_intx();

        u16 ctrl = msi_get_control();
        ctrl |= PCIR_MSI_CTRL_ME;

        // Mask all individual entries
        for (int i = 0; i< _msi.msi_msgnum; i++) {
            msi_mask_entry(i);
        }

        msi_set_control(ctrl);

        _msi_enabled = true;
    }


    void function::msix_disable()
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl &= ~PCIM_MSIXCTRL_MSIX_ENABLE;
        msix_set_control(ctrl);

        _msix_enabled = false;
    }

    void function::msi_disable()
    {
        if (!is_msi()) {
            return;
        }

        u16 ctrl = msi_get_control();
        ctrl &= ~PCIR_MSI_CTRL_ME;
        msi_set_control(ctrl);

        _msi_enabled = false;
    }

    void function::msix_set_control(u16 ctrl)
    {
        pci_writew(_msix.msix_location + PCIR_MSIX_CTRL, ctrl);
    }

    void function::msi_set_control(u16 ctrl)
    {
        pci_writew(_msi.msi_location + PCIR_MSI_CTRL, ctrl);
    }

    u16 function::msix_get_control()
    {
        return pci_readw(_msix.msix_location + PCIR_MSIX_CTRL);
    }

    u16 function::msi_get_control()
    {
        return pci_readw(_msi.msi_location + PCIR_MSI_CTRL);
    }

    mmioaddr_t function::msix_get_table()
    {
        bar* msix_bar = get_bar(_msix.msix_table_bar + 1);
        if (msix_bar == nullptr) {
            return mmio_nullptr;
        }

        return reinterpret_cast<mmioaddr_t>(msix_bar->get_mmio() +
                                              _msix.msix_table_offset);
    }

    u8 function::pci_readb(u8 offset)
    {
        return read_pci_config_byte(_bus, _device, _func, offset);
    }

    u16 function::pci_readw(u8 offset)
    {
        return read_pci_config_word(_bus, _device, _func, offset);
    }

    u32 function::pci_readl(u8 offset)
    {
        return read_pci_config(_bus, _device, _func, offset);
    }

    void function::pci_writeb(u8 offset, u8 val)
    {
        write_pci_config_byte(_bus, _device, _func, offset, val);
    }

    void function::pci_writew(u8 offset, u16 val)
    {
        write_pci_config_word(_bus, _device, _func, offset, val);
    }

    void function::pci_writel(u8 offset, u32 val)
    {
        write_pci_config(_bus, _device, _func, offset, val);
    }

    // Append to @cap_offs the offsets of all capabilities with id matching
    // @cap_id. Returns whether any such capabilities were found.
    bool function::find_capabilities(u8 cap_id, std::vector<u8>& cap_offs)
    {
        return find_capabilities(cap_id, cap_offs, true);
    }

    // Returns the offset of the first capability with id matching @cap_id, or
    // 0xFF if none found.
    u8 function::find_capability(u8 cap_id)
    {
        std::vector<u8> cap_offs;
        if (find_capabilities(cap_id, cap_offs, false)) {
            return cap_offs[0];
        } else {
            return 0xFF;
        }
    }

    // Append to @cap_offs the offsets of the first one or all capabilities with id matching
    // @cap_id. Returns whether any such capability/-ies were found.
    bool function::find_capabilities(u8 cap_id, std::vector<u8>& cap_offs, bool all)
    {
        u8 capabilities_base = pci_readb(PCI_CAPABILITIES_PTR);
        u8 off = capabilities_base;
        u8 max_capabilities = 0xF0;
        u8 ctr = 0;
        bool found = false;

        while (off != 0) {
            // Read capability
            u8 capability = pci_readb(off + PCI_CAP_OFF_ID);
            if (capability == cap_id) {
                cap_offs.push_back(off);
                if (all) {
                    found = true;
                } else {
                    return true;
                }
            }

            ctr++;
            if (ctr > max_capabilities) {
                return found;
            }

            // Next
            off = pci_readb(off + PCI_CAP_OFF_NEXT);
        }

        return found;
    }

    bar * function::get_bar(int idx)
    {
        auto it = _bars.find(idx);
        if (it == _bars.end()) {
            return nullptr;
        }

        return it->second;
    }

    void function::add_bar(int idx, bar * bar)
    {
        _bars.insert(std::make_pair(idx, bar));
    }

    void function::dump_config()
    {
        pci_d("[%x:%x.%x] vid:id = %x:%x",
            (u16)_bus, (u16)_device, (u16)_func, _vendor_id, _device_id);

        // PCI BARs
        for (int bar_idx = 1; bar_idx <= 6; bar_idx++) {
            bar *bar = get_bar(bar_idx);
            if (bar) {
                pci_d("    bar[%d]: %sbits addr=%p size=%x, mmio=%d",
                    bar_idx, (bar->is_64() ? "64" : "32"),
                    bar->get_addr64(), bar->get_size(), bar->is_mmio());
            }
        }

        pci_d("    IRQ = %d", (u16)get_interrupt_line());

        // MSI-x
        if (_have_msix) {
            pci_d("    Have MSI-X!");
            pci_d("        msix_location: %d", (u16)_msix.msix_location);
            pci_d("        msix_ctrl: %d", _msix.msix_ctrl);
            pci_d("        msix_msgnum: %d", _msix.msix_msgnum);
            pci_d("        msix_table_bar: %d", (u16)_msix.msix_table_bar);
            pci_d("        msix_table_offset: %d", _msix.msix_table_offset);
            pci_d("        msix_pba_bar: %d", (u16)_msix.msix_pba_bar);
            pci_d("        msix_pba_offset: %d", _msix.msix_pba_offset);
        }
    }
}
