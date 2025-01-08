#!/bin/bash

# Build the test module
make clean
make

# Check if build was successful
if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

# Remove any existing module
sudo rmmod test-tmt500rs 2>/dev/null

# Insert the test module
sudo insmod test-tmt500rs.ko

# Display test results from kernel log
dmesg | tail -n 20

# Clean up
sudo rmmod test-tmt500rs 