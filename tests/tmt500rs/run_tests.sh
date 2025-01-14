#!/bin/bash

# Colors for output
GREEN='\033[0;32m'
RED='\033[1;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
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

print_info() {
    echo -e "${BLUE}[i]${NC} $1"
}

print_phase() {
    echo -e "\n${CYAN}=== $1 ===${NC}"
}

# Function to save test logs
save_test_logs() {
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local log_dir="logs"
    mkdir -p "$log_dir"
    
    # Save dmesg output with sudo
    sudo dmesg | tail -n 1000 > "$log_dir/dmesg_${timestamp}.log"
    
    # Save test output
    if [ -n "$1" ]; then
        echo "$1" > "$log_dir/test_${timestamp}.log"
    fi
    
    print_info "Logs saved to $log_dir/dmesg_${timestamp}.log and $log_dir/test_${timestamp}.log"
}

# Function to check if a module is loaded
is_module_loaded() {
    lsmod | grep -q "^$1"
    return $?
}

# Function to check module dependencies
get_module_dependencies() {
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

# Function to display test results with colors
display_test_results() {
    local results=$(sudo dmesg | grep -A 15 "TMT500RS Test Summary:")
    local total_tests=$(echo "$results" | grep "Total test phases:" | awk '{print $4}' | cut -d'/' -f2)
    local completed_tests=$(echo "$results" | grep "Total test phases:" | awk '{print $4}' | cut -d'/' -f1)
    local total_assertions=$(echo "$results" | grep "Total assertions:" | awk '{print $3}')
    local passed=$(echo "$results" | grep "Passed:" | awk '{print $2}')
    local failed=$(echo "$results" | grep "Failed:" | awk '{print $2}')
    local success_rate=$(echo "$results" | grep "Success rate:" | awk '{print $3}' | tr -d '%')

    echo -e "\n${CYAN}=== Test Results ===${NC}"
    echo -e "Test Phases: ${GREEN}$completed_tests${NC}/$total_tests"
    echo -e "Total Assertions: ${GREEN}$total_assertions${NC}"
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

    # Display test categories
    echo -e "\n${CYAN}Test Categories:${NC}"
    echo "$results" | grep -A 4 "Test Categories:" | tail -n +2 | while read -r category; do
        if [ -n "$category" ]; then
            echo -e "${BLUE}$category${NC}"
        fi
    done
}

# Main test execution
print_phase "Starting Test Suite"

# Clean up any existing modules
cleanup_modules

# Clear kernel log before starting tests
print_status "Clearing kernel log..."
clear_dmesg

# Load test module with timeout
print_phase "Loading Test Module"

# First, try to find the module
if [ -f "../../test-tmt500rs.ko" ]; then
    print_status "Found test module in root directory"
    sudo insmod ../../test-tmt500rs.ko
elif [ -f "test-tmt500rs.ko" ]; then
    print_status "Found test module in current directory"
    sudo insmod test-tmt500rs.ko
else
    print_error "Could not find test module"
    exit 1
fi

if [ $? -ne 0 ]; then
    print_error "Failed to load test module"
    save_test_logs
    cleanup_modules
    exit 1
fi
print_success "Test module loaded"

# Wait for test completion (with timeout)
print_phase "Running Tests"
timeout=15
counter=0
test_started=false
test_output=""

while [ $counter -lt $timeout ]; do
    # Check for test start
    if ! $test_started && sudo dmesg | grep -q "Starting TMT500RS test module"; then
        test_started=true
        echo ""
    fi
    
    # If tests have started, show progress
    if $test_started; then
        current_test=$(sudo dmesg | grep "TMT500RS Test \[[0-9]*/[0-9]*\]:" | tail -n 1)
        if [ -n "$current_test" ]; then
            current=$(echo "$current_test" | grep -o "\[[0-9]*/[0-9]*\]" | cut -d'/' -f1 | tr -d '[]')
            total=$(echo "$current_test" | grep -o "\[[0-9]*/[0-9]*\]" | cut -d'/' -f2 | tr -d '[]')
            display_progress $current $total
        fi
    fi
    
    # Check for test completion
    if sudo dmesg | grep -q "TMT500RS Test Summary:"; then
        echo -e "\n"
        test_output=$(sudo dmesg | grep -A 20 "TMT500RS Test Summary:")
        display_test_results
        break
    fi
    
    sleep 1
    counter=$((counter + 1))
done

if [ $counter -ge $timeout ]; then
    print_error "Test timeout after ${timeout}s"
    save_test_logs "Test timeout after ${timeout}s"
    cleanup_modules
    exit 1
elif echo "$test_output" | grep -q "Failed: 0"; then
    print_success "All tests passed successfully"
    save_test_logs "$test_output"
    cleanup_modules
    exit 0
else
    print_error "Some tests failed"
    save_test_logs "$test_output"
    cleanup_modules
    exit 1
fi 