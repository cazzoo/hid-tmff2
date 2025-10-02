#!/bin/bash

echo "=========================================="
echo "T500RS Emergency Reset"
echo "=========================================="
echo ""

# Kill any running driver
echo "Stopping any running driver..."
sudo pkill -9 t500rs-ffb 2>/dev/null
sleep 1

# Compile a simple reset program
cat > /tmp/t500rs_reset.c << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libusb-1.0/libusb.h>

#define VENDOR_ID  0x044f
#define PRODUCT_ID 0xb65e

int main() {
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int ret;
    
    printf("Initializing libusb...\n");
    ret = libusb_init(&ctx);
    if (ret < 0) {
        fprintf(stderr, "Failed to initialize libusb: %s\n", libusb_error_name(ret));
        return 1;
    }
    
    printf("Opening T500RS device...\n");
    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        fprintf(stderr, "Failed to open device\n");
        libusb_exit(ctx);
        return 1;
    }
    
    printf("Detaching kernel driver...\n");
    libusb_detach_kernel_driver(handle, 0);
    
    printf("Claiming interface...\n");
    ret = libusb_claim_interface(handle, 0);
    if (ret < 0) {
        fprintf(stderr, "Failed to claim interface: %s\n", libusb_error_name(ret));
        libusb_close(handle);
        libusb_exit(ctx);
        return 1;
    }
    
    printf("\nSending emergency stop commands...\n");
    
    unsigned char buf[16];
    int transferred;
    
    // Stop all 16 effect slots
    for (int i = 0; i < 16; i++) {
        memset(buf, 0, sizeof(buf));
        buf[0] = 0x41;  // Stop effect command
        buf[1] = i;     // Effect ID
        buf[2] = 0x00;  // Stop
        buf[3] = 0x01;
        
        ret = libusb_interrupt_transfer(handle, 0x01, buf, 4, &transferred, 1000);
        if (ret == 0) {
            printf("  Stopped effect %d\n", i);
        }
        usleep(10000);
    }
    
    // Send zero Report 0x04 to clear ramp state
    printf("\nClearing ramp state...\n");
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x04;
    buf[1] = 0x0e;
    ret = libusb_interrupt_transfer(handle, 0x01, buf, 9, &transferred, 1000);
    if (ret == 0) {
        printf("  Ramp state cleared\n");
    }
    
    // Send zero Report 0x03 to clear force level
    printf("Clearing force level...\n");
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x03;
    buf[1] = 0x0e;
    ret = libusb_interrupt_transfer(handle, 0x01, buf, 4, &transferred, 1000);
    if (ret == 0) {
        printf("  Force level cleared\n");
    }
    
    printf("\n✅ Emergency reset complete!\n");
    printf("The wheel should now be stopped.\n\n");
    
    libusb_release_interface(handle, 0);
    libusb_close(handle);
    libusb_exit(ctx);
    
    return 0;
}
EOF

echo "Compiling reset tool..."
gcc -o /tmp/t500rs_reset /tmp/t500rs_reset.c -lusb-1.0

if [ $? -ne 0 ]; then
    echo "❌ Failed to compile reset tool"
    exit 1
fi

echo ""
echo "Running emergency reset..."
echo ""
sudo /tmp/t500rs_reset

echo ""
echo "=========================================="
echo "Reset complete!"
echo "=========================================="
echo ""
echo "The wheel should now be stopped."
echo "You can now start the driver normally:"
echo "  sudo ./run.sh"
echo ""

