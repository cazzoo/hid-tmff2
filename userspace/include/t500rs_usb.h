/**
 * @file t500rs_usb.h
 * @brief USB communication and device initialization
 * 
 * This module handles all USB communication with the T500RS device,
 * including device initialization, mode switching, and low-level
 * data transfer.
 */

#ifndef T500RS_USB_H
#define T500RS_USB_H

#include "t500rs_common.h"

/* ============================================================================
 * USB Communication
 * ============================================================================ */

/**
 * @brief Send data to the device via interrupt transfer
 * 
 * @param data Buffer containing data to send
 * @param len Length of data in bytes
 * @return 0 on success, negative on error
 */
int usb_send(const unsigned char *data, int len);

/**
 * @brief Receive data from the device via interrupt transfer
 * 
 * @param data Buffer to receive data
 * @param len Maximum length to receive
 * @param timeout Timeout in milliseconds
 * @return Number of bytes received, negative on error
 */
int usb_receive(unsigned char *data, int len, int timeout);

/* ============================================================================
 * Device Initialization
 * ============================================================================ */

/**
 * @brief Initialize T500RS device (mode switch if needed)
 * 
 * Sends initialization sequence to device. If device is in boot mode (b65d),
 * triggers mode switch to normal mode (b65e) via USB control transfer.
 * 
 * @return 0 on success, negative on error
 */
int t500rs_initialize(void);

/**
 * @brief Open and initialize USB device
 * 
 * Opens the T500RS device, detaches kernel driver, claims interface,
 * and performs mode switch if necessary.
 * 
 * @return 0 on success, negative on error
 */
int usb_device_open(void);

/**
 * @brief Close USB device and cleanup
 * 
 * Releases interface, reattaches kernel driver if needed, and closes device.
 */
void usb_device_close(void);

/**
 * @brief Wait for device to re-enumerate after mode switch
 * 
 * After mode switch, device disconnects and reconnects with new PID.
 * This function waits for the device to reappear in normal mode.
 * 
 * @param max_retries Maximum number of retry attempts
 * @return 0 on success, negative on error
 */
int usb_wait_for_reenumeration(int max_retries);

#endif /* T500RS_USB_H */

