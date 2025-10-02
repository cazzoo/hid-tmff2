/*
 * T500RS Force Feedback Test
 * 
 * Tests different force levels to verify FF is working
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

libusb_device_handle *handle = NULL;

void print_hex(const char *label, unsigned char *data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

int send_data(unsigned char *data, int len) {
    int ret, transferred;
    ret = libusb_interrupt_transfer(handle, EP_OUT, data, len, &transferred, 1000);
    if (ret < 0) {
        printf("❌ Transfer failed: %s\n", libusb_error_name(ret));
        return ret;
    }
    return 0;
}

int upload_constant_effect(int effect_id, int force_level) {
    int ret;
    
    printf("\nUploading constant effect %d with force level %d...\n", effect_id, force_level);
    
    // Calculate force bytes (16-bit little endian)
    unsigned char force_low = force_level & 0xff;
    unsigned char force_high = (force_level >> 8) & 0xff;
    
    // Report 0x02 (9 bytes) - from Windows
    unsigned char report_02[] = {0x02, 0x38, 0x00, 0x90, 0x01, 0x00, 0x52, 0x03, 0x00};
    print_hex("Report 0x02", report_02, sizeof(report_02));
    ret = send_data(report_02, sizeof(report_02));
    if (ret) return ret;
    usleep(5000);
    
    // Report 0x04 (8 bytes) - from Windows
    unsigned char report_04[] = {0x04, 0x2a, 0x00, 0x2c, 0x00, 0x00, 0x14, 0x00};
    print_hex("Report 0x04", report_04, sizeof(report_04));
    ret = send_data(report_04, sizeof(report_04));
    if (ret) return ret;
    usleep(5000);
    
    // Report 0x01 (15 bytes) - effect parameters
    unsigned char report_01[] = {
        0x01,           // Report ID
        effect_id,      // Effect ID
        0x22,           // Effect type (constant)
        0x40,           // Param (from Windows)
        force_low,      // Force level LOW byte
        force_high,     // Force level HIGH byte
        0x00,           // Param
        0xe8, 0x03,     // Params (from Windows)
        0x2a, 0x00,     // Params
        0x38, 0x00,     // Params
        0x00, 0x00      // Params
    };
    print_hex("Report 0x01", report_01, sizeof(report_01));
    ret = send_data(report_01, sizeof(report_01));
    if (ret) return ret;
    
    printf("✅ Effect uploaded\n");
    return 0;
}

int start_effect(int effect_id) {
    unsigned char start_cmd[] = {0x41, effect_id, 0x41, 0x01};
    printf("Starting effect %d...\n", effect_id);
    print_hex("Start cmd", start_cmd, sizeof(start_cmd));
    return send_data(start_cmd, sizeof(start_cmd));
}

int stop_effect(int effect_id) {
    unsigned char stop_cmd[] = {0x41, effect_id, 0x00, 0x01};
    printf("Stopping effect %d...\n", effect_id);
    return send_data(stop_cmd, sizeof(stop_cmd));
}

int main(int argc, char **argv) {
    libusb_context *ctx = NULL;
    int ret;
    
    printf("==========================================\n");
    printf("T500RS Force Feedback Test\n");
    printf("==========================================\n\n");
    
    // Initialize libusb
    ret = libusb_init(&ctx);
    if (ret < 0) {
        printf("❌ libusb_init failed: %s\n", libusb_error_name(ret));
        return ret;
    }
    
    // Open device
    printf("Opening T500RS...\n");
    handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!handle) {
        printf("❌ Cannot open device\n");
        libusb_exit(ctx);
        return -1;
    }
    printf("✅ Device opened\n\n");
    
    // Detach kernel driver
    if (libusb_kernel_driver_active(handle, INTERFACE) == 1) {
        printf("Detaching kernel driver...\n");
        ret = libusb_detach_kernel_driver(handle, INTERFACE);
        if (ret < 0) {
            printf("❌ Failed to detach: %s\n", libusb_error_name(ret));
            goto cleanup;
        }
    }
    
    // Claim interface
    ret = libusb_claim_interface(handle, INTERFACE);
    if (ret < 0) {
        printf("❌ Cannot claim interface: %s\n", libusb_error_name(ret));
        goto cleanup;
    }
    printf("✅ Interface claimed\n");
    
    // Test different force levels
    printf("\n==========================================\n");
    printf("Testing Force Levels\n");
    printf("==========================================\n");
    
    int force_levels[] = {
        0x1000,  // 4096  - Weak
        0x2000,  // 8192  - Medium (what we tried before)
        0x4000,  // 16384 - Strong
        0x7fff,  // 32767 - Maximum
    };
    
    const char *labels[] = {"Weak", "Medium", "Strong", "Maximum"};
    
    for (int i = 0; i < 4; i++) {
        printf("\n------------------------------------------\n");
        printf("Test %d: %s force (0x%04x = %d)\n", i+1, labels[i], force_levels[i], force_levels[i]);
        printf("------------------------------------------\n");
        
        // Upload effect
        ret = upload_constant_effect(1, force_levels[i]);
        if (ret) {
            printf("❌ Upload failed\n");
            break;
        }
        
        // Start effect
        ret = start_effect(1);
        if (ret) {
            printf("❌ Start failed\n");
            break;
        }
        
        printf("\n🎯 Effect active for 2 seconds...\n");
        printf("   >>> DO YOU FEEL ANYTHING? <<<\n");
        printf("   Try turning the wheel slightly\n\n");
        
        sleep(2);
        
        // Stop effect
        ret = stop_effect(1);
        if (ret) {
            printf("❌ Stop failed\n");
            break;
        }
        
        printf("✅ Effect stopped\n");
        
        if (i < 3) {
            printf("\nWaiting 1 second before next test...\n");
            sleep(1);
        }
    }
    
    printf("\n==========================================\n");
    printf("Test Complete\n");
    printf("==========================================\n\n");
    
    printf("Did you feel ANY of the force levels?\n");
    printf("- If YES: Force feedback works! 🎉\n");
    printf("- If NO: We may need to check:\n");
    printf("  1. Different effect types (spring, damper)\n");
    printf("  2. Initialization sequence\n");
    printf("  3. Device mode/configuration\n\n");
    
    // Cleanup
    libusb_release_interface(handle, INTERFACE);
    
cleanup:
    libusb_attach_kernel_driver(handle, INTERFACE);
    if (handle) {
        libusb_close(handle);
    }
    libusb_exit(ctx);
    
    return 0;
}

