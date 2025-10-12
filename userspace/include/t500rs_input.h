/**
 * @file t500rs_input.h
 * @brief Input handling for steering, pedals, buttons, and D-pad
 * 
 * This module handles all input from the T500RS device including:
 * - Steering wheel (16-bit precision)
 * - Pedals (throttle, brake, clutch)
 * - Buttons (16 buttons)
 * - D-pad (8 directions)
 */

#ifndef T500RS_INPUT_H
#define T500RS_INPUT_H

#include "t500rs_common.h"

/* ============================================================================
 * uinput Device Management
 * ============================================================================ */

/**
 * @brief Create and configure uinput device
 * 
 * Creates a virtual input device that applications can read from.
 * Configures all axes, buttons, and force feedback capabilities.
 * 
 * @return 0 on success, negative on error
 */
int input_device_create(void);

/**
 * @brief Destroy uinput device
 * 
 * Cleanly destroys the virtual input device.
 */
void input_device_destroy(void);

/* ============================================================================
 * Input Processing
 * ============================================================================ */

/**
 * @brief Process input report from device
 * 
 * Parses HID input report and sends appropriate events to uinput device.
 * Handles steering, pedals, buttons, and D-pad.
 * 
 * @param buf Input report buffer (Report ID 0x07)
 * @param len Length of input report
 */
void input_process_report(const unsigned char *buf, int len);

/**
 * @brief Start input reading thread
 * 
 * Starts background thread that continuously reads input from device
 * and processes it.
 * 
 * @return 0 on success, negative on error
 */
int input_thread_start(void);

/**
 * @brief Stop input reading thread
 * 
 * Stops the background input reading thread.
 */
void input_thread_stop(void);

#endif /* T500RS_INPUT_H */

