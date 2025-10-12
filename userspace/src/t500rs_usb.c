/**
 * @file t500rs_usb.c
 * @brief USB communication and device initialization
 * 
 * This module handles all USB communication with the T500RS device,
 * including device initialization, mode switching, and low-level
 * data transfer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../include/t500rs_usb.h"
#include "../include/t500rs_common.h"

/* USB endpoints */
#define EP_OUT  0x01
#define EP_IN   0x82

/* USB debug (set to 1 to enable hex dumps) */
#define USB_HEX_DEBUG 1

/* External running flag (from main) */
extern int running;

/* ============================================================================
 * USB Communication
 * ============================================================================ */

int usb_send(const unsigned char *data, int len)
{
    int ret, transferred;

#if USB_HEX_DEBUG
    /* Debug: print hex dump of outgoing packet */
    fprintf(stderr, "[USB OUT %2d] ", len);
    for (int i = 0; i < len; i++) {
        fprintf(stderr, "%02x", data[i]);
        if (i < len - 1 && (i + 1) % 4 == 0) {
            fprintf(stderr, " ");  /* Space every 4 bytes */
        }
    }
    fprintf(stderr, "\n");
#endif

    ret = libusb_interrupt_transfer(usb_handle, EP_OUT, (unsigned char *)data, len, &transferred, 1000);
    if (ret < 0) {
        /* Don't log NO_DEVICE errors during shutdown - these are expected */
        if (ret != LIBUSB_ERROR_NO_DEVICE && running) {
            LOG_ERROR("USB transfer failed: %s (ret=%d)", libusb_error_name(ret), ret);
        }
        return ret;
    }

    if (transferred != len) {
        LOG_ERROR("USB transfer incomplete: %d/%d bytes", transferred, len);
        return -1;
    }

#if USB_HEX_DEBUG
    fprintf(stderr, "[USB OK] Sent %d bytes successfully\n", transferred);
#endif

    return 0;
}

int usb_receive(unsigned char *data, int len, int timeout)
{
    int ret, transferred;

    ret = libusb_interrupt_transfer(usb_handle, EP_IN, data, len, &transferred, timeout);
    if (ret < 0) {
        if (ret != LIBUSB_ERROR_TIMEOUT && ret != LIBUSB_ERROR_NO_DEVICE) {
            LOG_ERROR("USB receive failed: %s", libusb_error_name(ret));
        }
        return ret;
    }

    return transferred;
}

/* ============================================================================
 * Device Initialization
 * ============================================================================ */

int t500rs_initialize(void)
{
    unsigned char buf[16];
    int ret;

    LOG_INFO("Initializing T500RS...");

    /* Report 0x42 - Init */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x42;
    buf[1] = 0x01;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(40000);

    /* Report 0x0a - Config 1 */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x0a;
    buf[1] = 0x04;
    buf[2] = 0x90;
    buf[3] = 0x03;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(4000);

    /* Report 0x0a - Config 2 */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x0a;
    buf[1] = 0x04;
    buf[2] = 0x12;
    buf[3] = 0x10;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(4000);

    /* Report 0x0a - Config 3 */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x0a;
    buf[1] = 0x04;
    buf[2] = 0x00;
    buf[3] = 0x06;
    ret = usb_send(buf, 15);
    if (ret) return ret;
    usleep(64000);

    /* Report 0x40 */
    buf[0] = 0x40;
    buf[1] = 0x11;
    buf[2] = 0x55;
    buf[3] = 0xd5;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(10000);

    /* Report 0x42 */
    buf[0] = 0x42;
    buf[1] = 0x04;
    ret = usb_send(buf, 2);
    if (ret) return ret;
    usleep(8000);

    /* Report 0x40 */
    buf[0] = 0x40;
    buf[1] = 0x04;
    buf[2] = 0x00;
    buf[3] = 0x00;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(8000);

    /* Report 0x43 */
    buf[0] = 0x43;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(8000);

    /* Report 0x41 */
    buf[0] = 0x41;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(8000);

    /* Report 0x40 */
    buf[0] = 0x40;
    buf[1] = 0x08;
    buf[2] = 0x00;
    buf[3] = 0x00;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(8000);

    /* Report 0x54 - Model query */
    buf[0] = 0x54;
    buf[1] = 0x00;
    buf[2] = 0x00;
    buf[3] = 0x00;
    ret = usb_send(buf, 4);
    if (ret) return ret;
    usleep(10000);

    /* Try to read model response */
    unsigned char model_response[64];
    ret = usb_receive(model_response, sizeof(model_response), 100);
    if (ret >= 2) {
        LOG_INFO("Response type: 0x%02x%02x", model_response[1], model_response[0]);
    }
    
    /* USB CONTROL REQUEST - Switch Mode
     * This triggers the actual mode switch from boot mode to normal mode
     * This is a vendor-specific USB control transfer that was in the original working code!
     * 
     * bRequestType: 0x41 (host-to-device, vendor, device)
     * bRequest: 83
     * wValue: 0x0002 (T500RS switch value)
     * wIndex: 0
     * wLength: 0
     */
    LOG_INFO("Sending mode switch control request (value=0x0002)...");
    
    ret = libusb_control_transfer(usb_handle,
        0x41,  /* bmRequestType: OUT, vendor, device */
        83,    /* bRequest */
        0x0002,/* wValue - T500RS switch value */
        0,     /* wIndex */
        NULL,  /* no data */
        0,     /* wLength */
        5000); /* timeout ms */
    
    if (ret < 0) {
        /* Device may disconnect before responding - this is normal */
        if (ret == LIBUSB_ERROR_NO_DEVICE || ret == LIBUSB_ERROR_PIPE || ret == LIBUSB_ERROR_IO) {
            LOG_INFO("Device disconnected during mode switch (expected behavior)");
        } else {
            LOG_ERROR("Mode switch control request failed: %s", libusb_error_name(ret));
            return ret;
        }
    } else {
        LOG_INFO("Mode switch control request sent successfully");
    }

    LOG_INFO("Initialization complete (mode switch triggered)");
    return 0;
}

/* ============================================================================
 * Device Management
 * ============================================================================ */

int usb_device_open(void)
{
    int ret;

    /* Initialize libusb */
    LOG_INFO("Initializing libusb...");
    ret = libusb_init(&usb_ctx);
    if (ret < 0) {
        LOG_ERROR("libusb_init failed: %s", libusb_error_name(ret));
        return 1;
    }

    /* Open device - try normal mode first, then boot mode */
    LOG_INFO("Opening T500RS device...");
    usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID);
    if (!usb_handle) {
        LOG_INFO("Normal mode device not found, trying boot mode...");
        usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID_BOOT);
        if (!usb_handle) {
            LOG_ERROR("Cannot open device (tried VID=%04x, PID=%04x and %04x)",
                     VENDOR_ID, PRODUCT_ID, PRODUCT_ID_BOOT);
            LOG_ERROR("Make sure the device is connected and you have permissions");
            return 1;
        }
        LOG_INFO("Opened in boot mode (will initialize to normal mode)");
    }
    LOG_INFO("Device opened successfully");

    /* Detach kernel driver */
    LOG_INFO("Detaching kernel driver...");
    ret = libusb_detach_kernel_driver(usb_handle, INTERFACE);
    if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
        LOG_ERROR("Failed to detach kernel driver: %s", libusb_error_name(ret));
        return 1;
    }
    if (ret == LIBUSB_ERROR_NOT_FOUND) {
        LOG_INFO("No kernel driver was attached");
    } else {
        LOG_INFO("Kernel driver detached successfully");
    }

    /* Claim interface */
    LOG_INFO("Claiming USB interface...");
    ret = libusb_claim_interface(usb_handle, INTERFACE);
    if (ret < 0) {
        LOG_ERROR("Cannot claim interface: %s", libusb_error_name(ret));
        return 1;
    }

    /* Check if we opened in boot mode */
    struct libusb_device_descriptor desc;
    libusb_device *dev = libusb_get_device(usb_handle);
    if (!dev) {
        LOG_ERROR("Failed to get device");
        return 1;
    }

    ret = libusb_get_device_descriptor(dev, &desc);
    if (ret < 0) {
        LOG_ERROR("Failed to get device descriptor: %s", libusb_error_name(ret));
        return 1;
    }

    int was_boot_mode = (desc.idProduct == PRODUCT_ID_BOOT);

    /* If in boot mode, send initialization sequence to trigger mode switch */
    if (was_boot_mode) {
        LOG_INFO("Device in boot mode, will send initialization sequence...");

        /* Initialize device (includes mode switch) */
        ret = t500rs_initialize();
        if (ret < 0) {
            LOG_ERROR("Device initialization failed");
            return 1;
        }

        /* Wait for re-enumeration */
        return usb_wait_for_reenumeration(10);
    } else {
        LOG_INFO("Device already in normal mode, skipping mode switch");
        /* Device is already in normal mode, just send basic init commands */
        unsigned char init_buf[15];

        /* Report 0x40 - Initialize FFB system */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x11;
        init_buf[2] = 0x55;
        init_buf[3] = 0xd5;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send init command 1");
        }
        usleep(10000);

        /* Report 0x42 - Configuration */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x42;
        init_buf[1] = 0x04;
        ret = usb_send(init_buf, 2);
        if (ret) {
            LOG_ERROR("Failed to send init command 2");
        }
        usleep(8000);

        /* Report 0x40 - Enable FFB */
        memset(init_buf, 0, sizeof(init_buf));
        init_buf[0] = 0x40;
        init_buf[1] = 0x04;
        init_buf[2] = 0x00;
        init_buf[3] = 0x00;
        ret = usb_send(init_buf, 4);
        if (ret) {
            LOG_ERROR("Failed to send init command 3");
        }
        usleep(8000);

        LOG_INFO("Normal mode initialization complete");
    }

    return 0;
}

int usb_wait_for_reenumeration(int max_retries)
{
    LOG_INFO("Device was in boot mode - it should now re-enumerate as normal mode");
    LOG_INFO("Waiting for device to disconnect and reconnect...");

    /* Try to detect disconnection */
    int disconnected = 0;
    for (int i = 0; i < 10; i++) {
        unsigned char test_buf[1];
        int test_ret = libusb_control_transfer(usb_handle,
            LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE,
            LIBUSB_REQUEST_GET_STATUS,
            0, 0, test_buf, 1, 100);

        if (test_ret < 0) {
            LOG_INFO("Device disconnected (attempt %d/10)", i+1);
            disconnected = 1;
            break;
        }
        usleep(100000);  /* Wait 100ms between checks */
    }

    if (!disconnected) {
        LOG_INFO("Device did not disconnect as expected");
        LOG_INFO("Trying to reopen anyway...");
    }

    /* Release interface and close current handle to allow re-enumeration */
    if (usb_handle) {
        LOG_DEBUG("Releasing interface before re-enumeration...");
        libusb_release_interface(usb_handle, INTERFACE);
        libusb_close(usb_handle);
        usb_handle = NULL;
    }

    /* Wait for device to re-enumerate */
    LOG_INFO("Waiting for device to re-enumerate in normal mode...");
    sleep(1);

    /* Try to open in normal mode with retries */
    for (int retry = 0; retry < max_retries; retry++) {
        usb_handle = libusb_open_device_with_vid_pid(usb_ctx, VENDOR_ID, PRODUCT_ID);
        if (usb_handle) {
            LOG_INFO("Device switched to normal mode successfully! (attempt %d/%d)", retry+1, max_retries);
            break;
        }

        if (retry < max_retries - 1) {
            LOG_INFO("Normal mode device not found yet, waiting... (attempt %d/%d)", retry+1, max_retries);
            sleep(1);
        }
    }

    if (!usb_handle) {
        LOG_ERROR("Device did not switch to normal mode after %d attempts!", max_retries);
        LOG_ERROR("Current device status:");
        system("lsusb | grep -i thrust");
        LOG_ERROR("");
        LOG_ERROR("If device shows as 044f:b65d, the mode switch failed.");
        LOG_ERROR("Please try:");
        LOG_ERROR("  1. Unplug the wheel USB cable");
        LOG_ERROR("  2. Wait 10 seconds");
        LOG_ERROR("  3. Plug it back in");
        LOG_ERROR("  4. Run: lsusb | grep -i thrust");
        LOG_ERROR("  5. If it shows 044f:b65e, start the driver again");
        return 1;
    }

    /* Detach kernel driver again */
    LOG_INFO("Detaching kernel driver from normal mode device...");
    int ret = libusb_detach_kernel_driver(usb_handle, INTERFACE);
    if (ret < 0 && ret != LIBUSB_ERROR_NOT_FOUND) {
        LOG_ERROR("Failed to detach kernel driver: %s", libusb_error_name(ret));
        return 1;
    }

    /* Claim interface again */
    LOG_INFO("Claiming interface on normal mode device...");
    ret = libusb_claim_interface(usb_handle, INTERFACE);
    if (ret < 0) {
        LOG_ERROR("Cannot claim interface: %s", libusb_error_name(ret));
        return 1;
    }

    LOG_INFO("Device successfully switched to normal mode and ready!");
    return 0;
}

void usb_device_close(void)
{
    if (usb_handle) {
        libusb_release_interface(usb_handle, INTERFACE);
        libusb_close(usb_handle);
        usb_handle = NULL;
    }

    if (usb_ctx) {
        libusb_exit(usb_ctx);
        usb_ctx = NULL;
    }
}

