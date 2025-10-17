/**
 * @file t500rs_bridge.h
 * @brief Wine Bridge IPC client interface
 */

#ifndef T500RS_BRIDGE_H
#define T500RS_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

/**
 * Initialize the bridge system
 * @return 0 on success, -1 on failure
 */
int bridge_init(void);

/**
 * Cleanup bridge resources
 */
void bridge_cleanup(void);

/**
 * Connect to the Wine bridge proxy
 * @return 0 on success, -1 on failure
 */
int bridge_connect(void);

/**
 * Disconnect from the Wine bridge proxy
 */
void bridge_disconnect(void);

/**
 * Check if bridge is connected
 * @return 1 if connected, 0 if not
 */
int bridge_is_connected(void);

/**
 * Send input report to Wine bridge
 * @param data Input report data
 * @param len Length of data
 * @return 0 on success, -1 on failure
 */
int bridge_send_input(const uint8_t *data, size_t len);

/**
 * Receive and process messages from Wine bridge
 * @return Number of messages processed, or -1 on error
 */
int bridge_process_messages(void);

#endif /* T500RS_BRIDGE_H */
