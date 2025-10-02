/*
 * T500RS libusb Test - Bypass HID Layer
 * 
 * This tests if we can communicate with the T500RS by bypassing
 * the kernel HID driver and using direct USB access.
 */

#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VENDOR_ID  0x044f
#define PRODUCT_ID 0xb65e
#define EP_OUT     0x01
#define INTERFACE  0

void print_hex(const char *label, unsigned char *data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    libusb_context *ctx = NULL;
    libusb_device_handle *handle = NULL;
    int ret, transferred;
    int test_mode = 0; // 0 = safe (stop only), 1 = params, 2 = full
    
    if (argc > 1) {
        test_mode = atoi(argv[1]);
    }
    
    printf("==========================================\n");
    printf("T500RS libusb Communication Test\n");
    printf("==========================================\n\n");
    
    printf("Test mode: %d (0=stop only, 1=params, 2=full)\n\n", test_mode);
    
    // Initialize libusb
    ret = libusb_init(&ctx);
    if (ret < 0) {
        printf("❌ libusb_init failed: %s\n", libusb_error_name(ret));
        return ret;
    }
    
    printf("[1/5] Opening T500RS device...\n");
    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        printf("❌ Cannot open device (VID=%04x, PID=%04x)\n", VENDOR_ID, PRODUCT_ID);
        printf("   Make sure device is connected and you have permissions\n");
        libusb_exit(ctx);
        return -1;
    }
    printf("✅ Device opened\n\n");
    
    // Check if kernel driver is active
    printf("[2/5] Checking kernel driver...\n");
    if (libusb_kernel_driver_active(handle, INTERFACE) == 1) {
        printf("⚠️  Kernel driver is active, detaching...\n");
        ret = libusb_detach_kernel_driver(handle, INTERFACE);
        if (ret < 0) {
            printf("❌ Failed to detach kernel driver: %s\n", libusb_error_name(ret));
            printf("   Try: sudo rmmod hid_tmff_new\n");
            goto cleanup;
        }
        printf("✅ Kernel driver detached\n");
    } else {
        printf("✅ No kernel driver active\n");
    }
    printf("\n");
    
    // Claim interface
    printf("[3/5] Claiming interface %d...\n", INTERFACE);
    ret = libusb_claim_interface(handle, INTERFACE);
    if (ret < 0) {
        printf("❌ Cannot claim interface: %s\n", libusb_error_name(ret));
        goto cleanup;
    }
    printf("✅ Interface claimed\n\n");
    
    // Test 1: Send stop command (safest)
    printf("[4/5] Test 1: Sending STOP command (safest)...\n");
    unsigned char stop_cmd[] = {0x41, 0x00, 0x00, 0x01};
    print_hex("   Data", stop_cmd, sizeof(stop_cmd));
    
    ret = libusb_interrupt_transfer(handle, EP_OUT, stop_cmd, 
                                   sizeof(stop_cmd), &transferred, 1000);
    
    if (ret == 0) {
        printf("   ✅ SUCCESS! Sent %d bytes\n", transferred);
        printf("   🎉 libusb works! We can bypass HID!\n\n");
    } else {
        printf("   ❌ Failed: %s\n", libusb_error_name(ret));
        printf("   This is unexpected - libusb should work\n\n");
        goto release;
    }
    
    // Test 2: Send parameter upload (if mode >= 1)
    if (test_mode >= 1) {
        printf("[5/5] Test 2: Sending parameter upload...\n");
        
        // Report 0x02 (9 bytes)
        unsigned char report_02[] = {0x02, 0x38, 0x00, 0x90, 0x01, 0x00, 0x52, 0x03, 0x00};
        print_hex("   Report 0x02", report_02, sizeof(report_02));
        
        ret = libusb_interrupt_transfer(handle, EP_OUT, report_02,
                                       sizeof(report_02), &transferred, 1000);
        if (ret == 0) {
            printf("   ✅ Report 0x02 sent: %d bytes\n", transferred);
        } else {
            printf("   ❌ Report 0x02 failed: %s\n", libusb_error_name(ret));
            goto release;
        }
        
        usleep(10000); // 10ms delay
        
        // Report 0x04 (8 bytes)
        unsigned char report_04[] = {0x04, 0x2a, 0x00, 0x2c, 0x00, 0x00, 0x14, 0x00};
        print_hex("   Report 0x04", report_04, sizeof(report_04));
        
        ret = libusb_interrupt_transfer(handle, EP_OUT, report_04,
                                       sizeof(report_04), &transferred, 1000);
        if (ret == 0) {
            printf("   ✅ Report 0x04 sent: %d bytes\n", transferred);
        } else {
            printf("   ❌ Report 0x04 failed: %s\n", libusb_error_name(ret));
            goto release;
        }
        
        usleep(10000); // 10ms delay
        
        // Report 0x01 (15 bytes)
        unsigned char report_01[] = {0x01, 0x01, 0x22, 0x40, 0xe2, 0x04, 0x00, 
                                    0xe8, 0x03, 0x2a, 0x00, 0x38, 0x00, 0x00, 0x00};
        print_hex("   Report 0x01", report_01, sizeof(report_01));
        
        ret = libusb_interrupt_transfer(handle, EP_OUT, report_01,
                                       sizeof(report_01), &transferred, 1000);
        if (ret == 0) {
            printf("   ✅ Report 0x01 sent: %d bytes\n", transferred);
            printf("   🎉 Parameter upload successful!\n\n");
        } else {
            printf("   ❌ Report 0x01 failed: %s\n", libusb_error_name(ret));
            goto release;
        }
        
        // Test 3: Send start command (if mode >= 2)
        if (test_mode >= 2) {
            printf("Test 3: Sending START command...\n");
            unsigned char start_cmd[] = {0x41, 0x01, 0x41, 0x01};
            print_hex("   Data", start_cmd, sizeof(start_cmd));
            
            ret = libusb_interrupt_transfer(handle, EP_OUT, start_cmd,
                                           sizeof(start_cmd), &transferred, 1000);
            if (ret == 0) {
                printf("   ✅ START command sent: %d bytes\n", transferred);
                printf("   🎉🎉🎉 FORCE FEEDBACK SHOULD BE WORKING NOW!\n");
                printf("   Check if you can feel the effect in the wheel!\n\n");
                
                // Wait a bit
                printf("Waiting 3 seconds...\n");
                sleep(3);
                
                // Send stop
                printf("Sending STOP command...\n");
                unsigned char stop_effect[] = {0x41, 0x01, 0x00, 0x01};
                libusb_interrupt_transfer(handle, EP_OUT, stop_effect,
                                         sizeof(stop_effect), &transferred, 1000);
                printf("✅ Effect stopped\n\n");
            } else {
                printf("   ❌ START command failed: %s\n", libusb_error_name(ret));
            }
        }
    }
    
    printf("==========================================\n");
    printf("RESULTS\n");
    printf("==========================================\n\n");
    
    if (ret == 0) {
        printf("✅ All tests passed!\n\n");
        printf("Next steps:\n");
        printf("1. If you felt force feedback, IT WORKS! 🎉\n");
        printf("2. Build a full userspace driver using libusb\n");
        printf("3. Or modify kernel driver to use libusb approach\n\n");
    } else {
        printf("⚠️  Some tests failed\n");
        printf("Check the errors above for details\n\n");
    }
    
release:
    // Release interface
    libusb_release_interface(handle, INTERFACE);
    
cleanup:
    // Reattach kernel driver
    libusb_attach_kernel_driver(handle, INTERFACE);
    
    // Close device
    if (handle) {
        libusb_close(handle);
    }
    
    // Cleanup libusb
    libusb_exit(ctx);
    
    printf("==========================================\n");
    printf("Test complete\n");
    printf("==========================================\n");
    
    return ret;
}

