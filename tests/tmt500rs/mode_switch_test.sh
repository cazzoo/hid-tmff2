#!/bin/bash

# Colors for status messages
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

print_status() {
    local color=$1
    local message=$2
    echo -e "${color}${message}${NC}"
}

cleanup() {
    echo "Cleaning up..."
    # Unload modules in reverse dependency order
    sudo modprobe -r test_tmt500rs 2>/dev/null
    sudo modprobe -r hid_tmt500rs 2>/dev/null
    sudo modprobe -r hid_tmff_new 2>/dev/null
    sudo modprobe -r hid_tminit_new 2>/dev/null
    sudo modprobe -r hid_tminit 2>/dev/null
    sudo modprobe -r hid_generic 2>/dev/null
    sudo modprobe -r usbhid 2>/dev/null
    sleep 2  # Wait for modules to fully unload
    sudo modprobe usbhid  # Ensure USB devices are detected
    sleep 1
}

load_module() {
    local module="$1"
    echo "Loading module $module..."
    if ! sudo modprobe "$module"; then
        echo "Failed to load module $module"
        return 1
    fi
    sleep 1
    return 0
}

wait_for_device() {
    local timeout=15
    local required_stable_time=3
    local stable_count=0
    local last_id=""
    local start_time=$(date +%s)
    
    echo "Waiting for device..."
    while true; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))
        
        if [ $elapsed -ge $timeout ]; then
            echo "Timeout waiting for device"
            return 1
        fi
        
        # Get current device ID
        local current_id=$(lsusb | grep "044f:b65" | head -n1)
        
        if [ -z "$current_id" ]; then
            echo "Device not found, waiting..."
            stable_count=0
            sleep 1
            continue
        fi
        
        if [ "$current_id" = "$last_id" ]; then
            stable_count=$((stable_count + 1))
            if [ $stable_count -ge $required_stable_time ]; then
                echo "Device stable for $required_stable_time seconds"
                return 0
            fi
        else
            echo "Device state changed: $current_id"
            stable_count=0
        fi
        
        last_id="$current_id"
        sleep 1
    done
}

wait_for_device_init() {
    local max_retries=30  # Increased timeout
    local retry_count=0

    # Wait for device to be initialized by either driver
    while [ $retry_count -lt $max_retries ]; do
        # Check if device exists and is initialized by either driver
        if (ls -l /sys/bus/hid/drivers/tmff2/*/mode 2>/dev/null | grep -q "mode" || \
            ls -l /sys/bus/hid/drivers/hid-tminit/*/mode 2>/dev/null | grep -q "mode"); then
            echo "Device initialized"
            return 0
        fi

        # Check if device is in init mode
        if lsusb | grep -q "044f:b65d"; then
            # Load init driver if not loaded
            if ! lsmod | grep -q "hid_tminit"; then
                echo "Loading init driver..."
                sudo modprobe hid_tminit
                sleep 2
            fi
        fi

        # Check if device is in wheel mode
        if lsusb | grep -q "044f:b65e"; then
            # Load wheel driver if not loaded
            if ! lsmod | grep -q "hid_tmff_new"; then
                echo "Loading wheel driver..."
                sudo modprobe hid_tmff_new
                sleep 2
            fi
        fi

        echo "Device not initialized, waiting..."
        sleep 1
        retry_count=$((retry_count + 1))
    done

    echo "Timeout waiting for device initialization"
    return 1
}

test_module_loading() {
    echo "Test 1: Loading modules..."
    
    # First ensure cleanup
    cleanup
    
    # Load required modules in correct order
    if load_module "usbhid" && \
       load_module "hid_tminit" && \
       load_module "hid_tmff_new"; then
        echo "Test 1: PASSED - Modules loaded successfully"
        return 0
    else
        echo "Test 1: FAILED - Module loading failed"
        cleanup
        return 1
    fi
}

test_device_detection() {
    echo "Test 2: Initial device detection..."
    
    if wait_for_device && wait_for_device_init; then
        echo "Test 2: PASSED - Device detected and initialized"
        return 0
    else
        echo "Test 2: FAILED - Device detection failed"
        return 1
    fi
}

test_mode_switch() {
    local device_path
    local max_retries=30  # Increased timeout
    local retry_count=0

    # Wait for device to be initialized
    if ! wait_for_device_init; then
        echo "Test 3: FAILED - Device initialization timeout"
        return 1
    fi

    # Find the device path (try both drivers)
    device_path=$(ls -l /sys/bus/hid/drivers/tmff2/*/mode 2>/dev/null | head -n1)
    if [ -z "$device_path" ]; then
        device_path=$(ls -l /sys/bus/hid/drivers/hid-tminit/*/mode 2>/dev/null | head -n1)
    fi

    if [ -z "$device_path" ]; then
        echo "Test 3: FAILED - Device node not found"
        return 1
    fi

    # Set wheel mode
    echo "Setting wheel mode..."
    echo "1" > "$device_path"
    sleep 5  # Increased wait time

    # Wait for mode switch to complete
    while [ $retry_count -lt $max_retries ]; do
        if lsusb | grep -q "044f:b65e"; then
            # Verify mode change
            local current_mode=$(cat "$device_path" 2>/dev/null)
            if [ "$current_mode" = "1" ]; then
                echo "Test 3: PASSED - Mode switch successful"
                return 0
            fi
        fi
        echo "Waiting for mode switch..."
        sleep 1
        retry_count=$((retry_count + 1))
    done

    echo "Test 3: FAILED - Mode switch failed"
    return 1
}

test_force_feedback() {
    echo "Test 4: Force feedback initialization..."
    
    # Wait for device to be ready
    sleep 5  # Increased wait time
    
    # Check if force feedback device exists
    if ls /dev/input/by-id/*event-joystick 2>/dev/null | grep -q "Thrustmaster"; then
        echo "Test 4: PASSED - Force feedback device found"
        return 0
    else
        echo "Test 4: FAILED - Force feedback device not found"
        return 1
    fi
}

test_error_recovery() {
    echo "Test 5: Error recovery..."
    
    # Check if device exists
    local device_path=$(ls -l /sys/bus/hid/drivers/*/mode 2>/dev/null | head -n1)
    if [ -z "$device_path" ]; then
        echo "Test 5: FAILED - Device node not found"
        return 1
    fi
    
    # Simulate disconnect/reconnect
    cleanup
    sleep 5  # Increased wait time
    
    # Reload modules in correct order
    if ! load_module "usbhid" || ! load_module "hid_tminit" || ! load_module "hid_tmff_new"; then
        echo "Test 5: FAILED - Module reload failed"
        return 1
    fi
    
    # Wait for device to be detected
    if ! wait_for_device || ! wait_for_device_init; then
        echo "Test 5: FAILED - Device recovery failed"
        return 1
    fi
    
    # Verify mode switch works after recovery
    if ! test_mode_switch; then
        echo "Test 5: FAILED - Mode switch after recovery failed"
        return 1
    fi
    
    # Verify force feedback works after recovery
    if ! test_force_feedback; then
        echo "Test 5: FAILED - Force feedback after recovery failed"
        return 1
    fi
    
    echo "Test 5: PASSED - Error recovery successful"
    return 0
}

# Main test sequence
echo -e "\nStarting T500RS mode switch tests..."

# Run all tests
test_module_loading
test_device_detection
test_mode_switch
test_force_feedback
test_error_recovery

# Final cleanup
cleanup
echo "Tests completed." 