#!/bin/bash

# Check if a PCIe BDF was provided
if [ -z "$1" ]; then
    echo "Usage: $0 <PCIe BDF (e.g., 0000:01:00.0)>"
    exit 1
fi

PCI_BDF="$1"

# Load the vfio-pci module
sudo modprobe vfio-pci

# Unbind the device from its current driver
echo "$PCI_BDF" | sudo tee /sys/bus/pci/devices/$PCI_BDF/driver/unbind

# Override the driver to vfio-pci
echo "vfio-pci" | sudo tee /sys/bus/pci/devices/$PCI_BDF/driver_override

# Bind the device to vfio-pci
echo "$PCI_BDF" | sudo tee /sys/bus/pci/drivers/vfio-pci/bind

# Verify the binding
lspci -k -s "$PCI_BDF"
