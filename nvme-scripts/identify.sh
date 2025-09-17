#!/bin/bash

# Iterate through all NVMe devices listed by lsblk
for device in $(lsblk -d -n -o NAME | grep '^nvme'); do
    echo "searching device $device"
	
    # Get the PCI ID for the current NVMe device
    pci_id=$(ls -l /sys/block/$device | awk -F'/' '{print $(NF-3)}')

    echo "found device $pci-id" with command "ls -l /sys/block/$device | awk -F'/' '{print $(NF-3)}'"

    # Get the model name using the nvme tool, removing newlines and extra spaces
    model_name=$(sudo nvme id-ctrl /dev/$device | grep 'mn' | awk -F': ' '{print $2}' | tr -d '\n' | xargs)

    # Get the kernel driver in use for the current PCI ID
    driver=$(lspci -k -s $pci_id | grep 'Kernel driver in use' | awk -F': ' '{print $2}' | xargs)

    # Output the result in the desired format
    echo "/dev/$device -> $pci_id ($model_name) [Driver: $driver]"
done

