/*
 * T500RS Force Feedback Test with Initialization
 * 
 * Sends the full initialization sequence from Windows before testing FF
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

int send_data(unsigned char *data, int len, const char *label) {
    int ret, transferred;
    if (label) {
        print_hex(label, data, len);
    }
    ret = libusb_interrupt_transfer(handle, EP_OUT, data, len, &transferred, 1000);
    if (ret < 0) {
        printf("  ❌ Failed: %s\n", libusb_error_name(ret));
        return ret;
    }
    printf("  ✅ Sent %d bytes\n", transferred);
    return 0;
}

int initialize_device() {
    int ret;
    
    printf("\n==========================================\n");
    printf("Initializing T500RS (Windows sequence)\n");
    printf("==========================================\n\n");
    
    // Frame 97: Report 0x42 - Init (15 bytes)
    printf("[1/8] Sending Report 0x42 (Init)...\n");
    unsigned char init_42[] = {0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ret = send_data(init_42, sizeof(init_42), "  Data");
    if (ret) return ret;
    usleep(40000); // 40ms delay like Windows
    
    // Frame 104: Report 0x0a - Config 1 (15 bytes)
    printf("\n[2/8] Sending Report 0x0a (Config 1)...\n");
    unsigned char config_0a_1[] = {0x0a, 0x04, 0x90, 0x03, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ret = send_data(config_0a_1, sizeof(config_0a_1), "  Data");
    if (ret) return ret;
    usleep(4000);
    
    // Frame 108: Report 0x0a - Config 2 (15 bytes)
    printf("\n[3/8] Sending Report 0x0a (Config 2)...\n");
    unsigned char config_0a_2[] = {0x0a, 0x04, 0x12, 0x10, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ret = send_data(config_0a_2, sizeof(config_0a_2), "  Data");
    if (ret) return ret;
    usleep(4000);
    
    // Frame 112: Report 0x0a - Config 3 (15 bytes)
    printf("\n[4/8] Sending Report 0x0a (Config 3)...\n");
    unsigned char config_0a_3[] = {0x0a, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    ret = send_data(config_0a_3, sizeof(config_0a_3), "  Data");
    if (ret) return ret;
    usleep(64000); // 64ms delay
    
    // Frame 165: Report 0x40 (4 bytes)
    printf("\n[5/8] Sending Report 0x40...\n");
    unsigned char report_40[] = {0x40, 0x11, 0x55, 0xd5};
    ret = send_data(report_40, sizeof(report_40), "  Data");
    if (ret) return ret;
    usleep(10000);
    
    // Frame 202: Report 0x42 short (2 bytes)
    printf("\n[6/8] Sending Report 0x42 (short)...\n");
    unsigned char init_42_short[] = {0x42, 0x04};
    ret = send_data(init_42_short, sizeof(init_42_short), "  Data");
    if (ret) return ret;
    usleep(8000);
    
    // Frame 204: Report 0x40 (4 bytes)
    printf("\n[7/8] Sending Report 0x40 (2)...\n");
    unsigned char report_40_2[] = {0x40, 0x04, 0x00, 0x00};
    ret = send_data(report_40_2, sizeof(report_40_2), "  Data");
    if (ret) return ret;
    usleep(8000);
    
    // Frame 206: Report 0x40 (4 bytes)
    printf("\n[8/8] Sending Report 0x40 (3)...\n");
    unsigned char report_40_3[] = {0x40, 0x03, 0x0d, 0x00};
    ret = send_data(report_40_3, sizeof(report_40_3), "  Data");
    if (ret) return ret;
    
    printf("\n✅ Initialization complete!\n");
    printf("   Device should now be in full force feedback mode\n\n");
    
    return 0;
}

int upload_constant_effect(int effect_id, int force_level) {
    int ret;
    
    printf("Uploading constant effect %d (force=0x%04x)...\n", effect_id, force_level);
    
    unsigned char force_low = force_level & 0xff;
    unsigned char force_high = (force_level >> 8) & 0xff;
    
    // Report 0x02
    unsigned char report_02[] = {0x02, 0x38, 0x00, 0x90, 0x01, 0x00, 0x52, 0x03, 0x00};
    ret = send_data(report_02, sizeof(report_02), NULL);
    if (ret) return ret;
    usleep(5000);
    
    // Report 0x04
    unsigned char report_04[] = {0x04, 0x2a, 0x00, 0x2c, 0x00, 0x00, 0x14, 0x00};
    ret = send_data(report_04, sizeof(report_04), NULL);
    if (ret) return ret;
    usleep(5000);
    
    // Report 0x01
    unsigned char report_01[] = {
        0x01, effect_id, 0x22, 0x40,
        force_low, force_high, 0x00,
        0xe8, 0x03, 0x2a, 0x00,
        0x38, 0x00, 0x00, 0x00
    };
    ret = send_data(report_01, sizeof(report_01), NULL);
    if (ret) return ret;
    
    printf("  ✅ Effect uploaded\n");
    return 0;
}

int start_effect(int effect_id) {
    unsigned char start_cmd[] = {0x41, effect_id, 0x41, 0x01};
    return send_data(start_cmd, sizeof(start_cmd), NULL);
}

int stop_effect(int effect_id) {
    unsigned char stop_cmd[] = {0x41, effect_id, 0x00, 0x01};
    return send_data(stop_cmd, sizeof(stop_cmd), NULL);
}

int main() {
    libusb_context *ctx = NULL;
    int ret;
    
    printf("==========================================\n");
    printf("T500RS Force Feedback Test\n");
    printf("WITH FULL INITIALIZATION\n");
    printf("==========================================\n\n");
    
    // Initialize libusb
    ret = libusb_init(&ctx);
    if (ret < 0) {
        printf("❌ libusb_init failed\n");
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
    printf("✅ Device opened\n");
    
    // Detach kernel driver
    if (libusb_kernel_driver_active(handle, INTERFACE) == 1) {
        libusb_detach_kernel_driver(handle, INTERFACE);
    }
    
    // Claim interface
    ret = libusb_claim_interface(handle, INTERFACE);
    if (ret < 0) {
        printf("❌ Cannot claim interface\n");
        goto cleanup;
    }
    printf("✅ Interface claimed\n");
    
    // INITIALIZE DEVICE
    ret = initialize_device();
    if (ret) {
        printf("❌ Initialization failed!\n");
        goto cleanup;
    }
    
    // Wait a bit after init
    printf("Waiting 1 second after initialization...\n\n");
    sleep(1);
    
    // Test maximum force
    printf("==========================================\n");
    printf("Testing MAXIMUM Force\n");
    printf("==========================================\n\n");
    
    ret = upload_constant_effect(1, 0x7fff);  // Maximum force
    if (ret) goto cleanup;
    
    printf("\nStarting effect...\n");
    ret = start_effect(1);
    if (ret) goto cleanup;
    
    printf("\n🎯🎯🎯 MAXIMUM FORCE ACTIVE! 🎯🎯🎯\n");
    printf("Duration: 5 seconds\n");
    printf(">>> TRY TO TURN THE WHEEL NOW! <<<\n");
    printf("You should feel STRONG resistance!\n\n");
    
    sleep(5);
    
    printf("Stopping effect...\n");
    stop_effect(1);
    
    printf("\n✅ Test complete!\n\n");
    
    printf("==========================================\n");
    printf("RESULTS\n");
    printf("==========================================\n\n");
    printf("Did you feel STRONG force this time?\n");
    printf("- If YES: Initialization was the missing piece! 🎉\n");
    printf("- If NO: May need different effect type or more config\n\n");
    
cleanup:
    libusb_release_interface(handle, INTERFACE);
    libusb_attach_kernel_driver(handle, INTERFACE);
    if (handle) {
        libusb_close(handle);
    }
    libusb_exit(ctx);
    
    return 0;
}

