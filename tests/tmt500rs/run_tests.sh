#!/bin/bash

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print progress
print_status() {
    echo -e "${YELLOW}[*]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

print_error() {
    echo -e "${RED}[✗]${NC} $1"
}

# Function to check if a module is loaded
is_module_loaded() {
    lsmod | grep -q "^$1"
    return $?
}

# Function to check module dependencies
get_module_dependencies() {
    local module=$1
    lsmod | grep "^$module" | awk '{print $4}' | tr ',' ' '
}

# Function to check if module is busy
is_module_busy() {
    local module=$1
    local deps=$(get_module_dependencies "$module")
    if [ -n "$deps" ]; then
        return 0 # Module has dependencies
    fi
    return 1
}

# Function to force remove a module with better error handling
force_remove_module() {
    local module=$1
    local max_retries=3
    local retry=0

    if is_module_loaded "$module"; then
        print_status "Force removing module $module..."
        while [ $retry -lt $max_retries ]; do
            if is_module_busy "$module"; then
                print_status "Module $module is busy, removing dependencies first..."
                local deps=$(get_module_dependencies "$module")
                for dep in $deps; do
                    force_remove_module "$dep"
                done
            fi
            
            # Try to remove normally first
            sudo rmmod "$module" 2>/dev/null
            if [ $? -eq 0 ] || ! is_module_loaded "$module"; then
                print_success "Module $module removed"
                return 0
            fi
            
            # If normal removal fails, try force removal
            sudo rmmod -f "$module" 2>/dev/null
            if [ $? -eq 0 ] || ! is_module_loaded "$module"; then
                print_success "Module $module force removed"
                return 0
            fi
            
            retry=$((retry + 1))
            if [ $retry -lt $max_retries ]; then
                print_status "Retrying removal of $module (attempt $((retry + 1))/$max_retries)..."
                sleep 1
            fi
        done
        print_error "Failed to remove $module after $max_retries attempts"
        return 1
    fi
    return 0
}

# Function to safely remove a module with timeout
remove_module() {
    local module=$1
    local timeout=5
    local counter=0

    if is_module_loaded "$module"; then
        print_status "Removing module $module..."
        if is_module_busy "$module"; then
            print_status "Module $module is busy, trying force removal..."
            force_remove_module "$module"
            return $?
        fi
        
        sudo modprobe -r "$module" 2>/dev/null
        while is_module_loaded "$module"; do
            sleep 1
            counter=$((counter + 1))
            if [ $counter -ge $timeout ]; then
                print_status "Normal removal timed out, trying force removal..."
                force_remove_module "$module"
                return $?
            fi
            echo -n "."
        done
        echo ""
        print_success "Module $module removed"
        return 0
    fi
    return 0
}

# Function to clean up modules
cleanup_modules() {
    print_status "Cleaning up modules..."
    
    # First try to remove test module
    if is_module_loaded "test_tmt500rs"; then
        print_status "Removing module test_tmt500rs..."
        remove_module "test_tmt500rs" || force_remove_module "test_tmt500rs"
    fi
    
    # Then remove main module
    if is_module_loaded "hid_tmff_new"; then
        print_status "Removing module hid_tmff_new..."
        remove_module "hid_tmff_new" || force_remove_module "hid_tmff_new"
    fi
    
    # Final verification
    if is_module_loaded "test_tmt500rs" || is_module_loaded "hid_tmff_new"; then
        print_error "Some modules could not be removed"
        return 1
    fi
    
    return 0
}

# Clear kernel log
clear_dmesg() {
    sudo dmesg -C
}

# Trap for cleanup on script exit
trap cleanup_modules EXIT

# Change to root directory
cd ../../
if [ $? -ne 0 ]; then
    print_error "Failed to change to root directory"
    exit 1
fi

# Clean build
print_status "Cleaning previous build..."
make clean
if [ $? -ne 0 ]; then
    print_error "Clean failed"
    exit 1
fi
print_success "Clean completed"

# Build modules
print_status "Building modules..."
make
if [ $? -ne 0 ]; then
    print_error "Build failed"
    exit 1
fi
print_success "Build completed"

# Remove existing modules
print_status "Removing existing modules..."
cleanup_modules

# Clear kernel log before starting tests
print_status "Clearing kernel log..."
clear_dmesg

# Install modules
print_status "Installing modules..."
sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/hid
sudo cp hid-tmff-new.ko /lib/modules/$(uname -r)/kernel/drivers/hid/
sudo cp test-tmt500rs.ko /lib/modules/$(uname -r)/kernel/drivers/hid/
sudo depmod -a

# Load main module
print_status "Loading main module..."
sudo modprobe hid-tmff-new
if [ $? -ne 0 ]; then
    print_error "Failed to load main module"
    exit 1
fi
print_success "Main module loaded"

# Function to display test progress bar
display_progress() {
    local current=$1
    local total=$2
    local width=50
    local progress=$((current * width / total))
    local percentage=$((current * 100 / total))
    
    printf "\r["
    for ((i=0; i<width; i++)); do
        if [ $i -lt $progress ]; then
            printf "="
        else
            printf " "
        fi
    done
    printf "] %d%%" $percentage
}

# Function to parse and display test results
display_test_results() {
    local results=$(dmesg | grep -A 10 "=== T500RS Driver Test Summary ===")
    local total_tests=$(echo "$results" | grep "Total test phases:" | awk '{print $4}' | cut -d'/' -f2)
    local completed_tests=$(echo "$results" | grep "Total test phases:" | awk '{print $4}' | cut -d'/' -f1)
    local total_assertions=$(echo "$results" | grep "Total assertions:" | awk '{print $3}')
    local passed=$(echo "$results" | grep "Passed:" | awk '{print $2}')
    local failed=$(echo "$results" | grep "Failed:" | awk '{print $2}')
    local success_rate=$(echo "$results" | grep "Success rate:" | awk '{print $3}' | tr -d '%')

    echo -e "\n\n${YELLOW}=== Test Results ===${NC}"
    echo -e "Test Phases: ${GREEN}$completed_tests${NC}/$total_tests"
    echo -e "Assertions: ${GREEN}$total_assertions${NC}"
    echo -e "Passed: ${GREEN}$passed${NC}"
    if [ "$failed" -eq "0" ]; then
        echo -e "Failed: ${GREEN}$failed${NC}"
    else
        echo -e "Failed: ${RED}$failed${NC}"
    fi
    
    if [ "$success_rate" -eq "100" ]; then
        echo -e "Success Rate: ${GREEN}$success_rate%${NC}"
    elif [ "$success_rate" -ge "80" ]; then
        echo -e "Success Rate: ${YELLOW}$success_rate%${NC}"
    else
        echo -e "Success Rate: ${RED}$success_rate%${NC}"
    fi
}

# Function to display test phase details
display_test_phase() {
    local phase=$1
    local message=$2
    echo -e "\n${YELLOW}Phase $phase:${NC} $message"
}

# Load test module with timeout
display_test_phase "1" "Loading test module"
sudo modprobe test-tmt500rs
if [ $? -ne 0 ]; then
    print_error "Failed to load test module"
    cleanup_modules
    exit 1
fi
print_success "Test module loaded"

# Wait for test completion (with timeout)
display_test_phase "2" "Running tests"
timeout=15
counter=0
test_started=false

while [ $counter -lt $timeout ]; do
    # Check for test start
    if ! $test_started && dmesg | grep -q "=== Starting T500RS Driver Test Suite ==="; then
        test_started=true
        echo ""
    fi
    
    # If tests have started, show progress
    if $test_started; then
        current_test=$(dmesg | grep "T500RS Test \[[0-9]*/[0-9]*\]:" | tail -n 1)
        if [ -n "$current_test" ]; then
            current=$(echo "$current_test" | grep -o "\[[0-9]*/[0-9]*\]" | cut -d'/' -f1 | tr -d '[]')
            total=$(echo "$current_test" | grep -o "\[[0-9]*/[0-9]*\]" | cut -d'/' -f2 | tr -d '[]')
            display_progress $current $total
        fi
    fi
    
    # Check for test completion
    if dmesg | grep -q "=== T500RS Driver Test Summary ==="; then
        echo -e "\n"
        display_test_results
        break
    fi
    
    sleep 1
    counter=$((counter + 1))
done

if [ $counter -ge $timeout ]; then
    print_error "Test timeout after ${timeout}s"
    dmesg | tail -n 30
fi

# Final status
if [ $counter -ge $timeout ]; then
    print_error "Tests did not complete within ${timeout} seconds"
    exit 1
elif dmesg | grep -q "Failed: 0"; then
    print_success "All tests passed successfully"
    exit 0
else
    print_error "Some tests failed"
    exit 1
fi 