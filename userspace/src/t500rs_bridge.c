/**
 * @file t500rs_bridge.c
 * @brief Wine Bridge IPC client
 * 
 * This module handles communication with the UHID proxy for Wine compatibility.
 * It forwards input reports to the proxy and receives force feedback commands.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <linux/input.h>
#include "../include/t500rs_logging.h"
#include "../include/t500rs_effects.h"

#define SOCKET_PATH "/tmp/t500rs_bridge.sock"
#define MAX_REPORT_SIZE 256
#define RECONNECT_DELAY_MS 1000

// IPC message types (must match proxy)
#define MSG_INPUT_REPORT  0x01
#define MSG_OUTPUT_REPORT 0x02
#define MSG_FF_REPORT     0x03

// IPC message structure (must match proxy)
struct bridge_message {
    uint8_t msg_type;
    uint8_t report_id;
    uint16_t data_len;
    uint8_t data[MAX_REPORT_SIZE];
} __attribute__((packed));

static int bridge_fd = -1;
static int bridge_enabled = 0;

// External effect state from main driver
extern struct effect_state effects[];
extern pthread_mutex_t effects_lock;

/**
 * Connect to the Wine bridge proxy
 * @return 0 on success, -1 on failure
 */
int bridge_connect(void)
{
    struct sockaddr_un addr;
    int flags;
    
    if (bridge_fd >= 0) {
        return 0;  // Already connected
    }
    
    bridge_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (bridge_fd < 0) {
        LOG_ERROR("Failed to create bridge socket: %s", strerror(errno));
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(bridge_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_DEBUG("Bridge not available: %s", strerror(errno));
        close(bridge_fd);
        bridge_fd = -1;
        return -1;
    }
    
    // Set non-blocking mode
    flags = fcntl(bridge_fd, F_GETFL, 0);
    fcntl(bridge_fd, F_SETFL, flags | O_NONBLOCK);
    
    LOG_INFO("Connected to Wine bridge proxy");
    bridge_enabled = 1;
    
    return 0;
}

/**
 * Disconnect from the Wine bridge proxy
 */
void bridge_disconnect(void)
{
    if (bridge_fd >= 0) {
        close(bridge_fd);
        bridge_fd = -1;
        bridge_enabled = 0;
        LOG_INFO("Disconnected from Wine bridge proxy");
    }
}

/**
 * Check if bridge is connected
 * @return 1 if connected, 0 if not
 */
int bridge_is_connected(void)
{
    return (bridge_fd >= 0 && bridge_enabled);
}

/**
 * Send input report to Wine bridge
 * @param data Input report data
 * @param len Length of data
 * @return 0 on success, -1 on failure
 */
int bridge_send_input(const uint8_t *data, size_t len)
{
    struct bridge_message msg;
    ssize_t ret;
    
    if (!bridge_is_connected()) {
        return -1;
    }
    
    if (len > MAX_REPORT_SIZE) {
        LOG_ERROR("Input report too large: %zu bytes", len);
        return -1;
    }
    
    msg.msg_type = MSG_INPUT_REPORT;
    msg.report_id = 0;
    msg.data_len = len;
    memcpy(msg.data, data, len);
    
    size_t total_len = sizeof(msg.msg_type) + sizeof(msg.report_id) + 
                       sizeof(msg.data_len) + len;
    
    ret = send(bridge_fd, &msg, total_len, MSG_DONTWAIT);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  // Would block, not an error
        }
        LOG_ERROR("Failed to send to bridge: %s", strerror(errno));
        bridge_disconnect();
        return -1;
    }
    
    return 0;
}

/**
 * Receive and process messages from Wine bridge
 * This should be called periodically from the main loop
 * @return Number of messages processed, or -1 on error
 */
int bridge_process_messages(void)
{
    struct bridge_message msg;
    ssize_t ret;
    int count = 0;
    
    if (!bridge_is_connected()) {
        return 0;
    }
    
    // Process all available messages
    while (1) {
        ret = recv(bridge_fd, &msg, sizeof(msg), MSG_DONTWAIT);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // No more messages
            }
            LOG_ERROR("Failed to receive from bridge: %s", strerror(errno));
            bridge_disconnect();
            return -1;
        }
        
        if (ret == 0) {
            LOG_INFO("Bridge connection closed by proxy");
            bridge_disconnect();
            return -1;
        }
        
        // Process message based on type
        switch (msg.msg_type) {
        case MSG_OUTPUT_REPORT:
            LOG_DEBUG("Bridge: Output report received (%u bytes)", msg.data_len);
            // TODO: Forward to device if needed
            break;
            
        case MSG_FF_REPORT:
            // Process HID PID force feedback reports from Wine
            if (msg.report_id == 0x01 && msg.data_len >= 14) {
                // Set Effect Report
                uint8_t effect_id = msg.data[0];
                uint8_t effect_type = msg.data[1];
                
                LOG_INFO("Bridge: Set Effect (id=%u, type=%u)", effect_id, effect_type);
                
                // Only handle effect IDs in valid range
                if (effect_id >= 1 && effect_id <= MAX_EFFECTS) {
                    int id = effect_id - 1; // Convert 1-based to 0-based
                    
                    // Create a Linux ff_effect structure
                    struct ff_effect effect;
                    memset(&effect, 0, sizeof(effect));
                    effect.id = id;
                    
                    // Parse HID PID effect type (0x26 = Constant Force)
                    if (effect_type == 0x26) {
                        effect.type = FF_CONSTANT;
                        
                        // Parse duration (bytes 2-3, little-endian, in ms)
                        uint16_t duration_ms = msg.data[2] | (msg.data[3] << 8);
                        effect.replay.length = duration_ms;
                        effect.replay.delay = 0;
                        
                        // Parse gain (byte 8, 0-255 → 0-65535)
                        uint8_t gain_8bit = msg.data[8];
                        uint16_t gain = (gain_8bit * 65535) / 255;
                        
                        // For constant force, magnitude comes from later Set Envelope
                        // or Set Constant Force reports. Use a default for now.
                        // Typical HID PID uses -10000 to +10000 range
                        // We'll use gain as a base magnitude
                        int16_t magnitude = (int16_t)((gain * 32767) / 65535);
                        effect.u.constant.level = magnitude;
                        
                        // Set envelope (attack/fade)
                        effect.u.constant.envelope.attack_length = 0;
                        effect.u.constant.envelope.attack_level = 0;
                        effect.u.constant.envelope.fade_length = 0;
                        effect.u.constant.envelope.fade_level = 0;
                        
                        LOG_INFO("  → Constant Force: magnitude=%d, duration=%ums", 
                                 magnitude, duration_ms);
                        
                        // Upload the effect
                        pthread_mutex_lock(&effects_lock);
                        effects[id].effect = effect;
                        upload_constant_effect(id, &effect);
                        pthread_mutex_unlock(&effects_lock);
                    }
                    else if (effect_type >= 0x40 && effect_type <= 0x43) {
                        // Condition effects (Spring, Damper, Inertia, Friction)
                        LOG_INFO("  → Condition Effect (type=0x%02x) - implementing...", effect_type);
                        // Map to Linux condition effect types
                        if (effect_type == 0x40) effect.type = FF_SPRING;
                        else if (effect_type == 0x41) effect.type = FF_DAMPER;
                        else if (effect_type == 0x42) effect.type = FF_INERTIA;
                        else if (effect_type == 0x43) effect.type = FF_FRICTION;
                        
                        // Basic condition parameters
                        effect.replay.length = 0xFFFF; // Infinite
                        effect.replay.delay = 0;
                        
                        // Default spring/damper coefficients
                        effect.u.condition[0].right_saturation = 0x7FFF;
                        effect.u.condition[0].left_saturation = 0x7FFF;
                        effect.u.condition[0].right_coeff = 0x4000;
                        effect.u.condition[0].left_coeff = 0x4000;
                        effect.u.condition[0].deadband = 0;
                        effect.u.condition[0].center = 0;
                        
                        pthread_mutex_lock(&effects_lock);
                        effects[id].effect = effect;
                        upload_condition_effect(id, &effect);
                        pthread_mutex_unlock(&effects_lock);
                    }
                    else {
                        LOG_INFO("  → Unsupported effect type: 0x%02x", effect_type);
                    }
                }
            }
            else if (msg.report_id == 0x02 && msg.data_len >= 3) {
                // Effect Operation Report
                uint8_t effect_id = msg.data[0];
                uint8_t operation = msg.data[1];
                // uint8_t loop_count = msg.data[2];
                
                LOG_INFO("Bridge: Effect Operation (id=%u, op=%u)", effect_id, operation);
                
                if (effect_id >= 1 && effect_id <= MAX_EFFECTS) {
                    int id = effect_id - 1; // Convert 1-based to 0-based
                    
                    if (operation == 0x01 || operation == 0x02) {
                        // Start or Start Solo
                        LOG_INFO("  → Starting effect %d", id);
                        pthread_mutex_lock(&effects_lock);
                        start_effect(id);
                        effects[id].active = 1;
                        pthread_mutex_unlock(&effects_lock);
                    }
                    else if (operation == 0x03) {
                        // Stop
                        LOG_INFO("  → Stopping effect %d", id);
                        pthread_mutex_lock(&effects_lock);
                        stop_effect(id);
                        effects[id].active = 0;
                        pthread_mutex_unlock(&effects_lock);
                    }
                }
            }
            else {
                // Log unknown FF reports for debugging
                LOG_DEBUG("Bridge: Unknown FF report (id=%u, %u bytes)", 
                         msg.report_id, msg.data_len);
            }
            break;
            
        default:
            LOG_WARN("Bridge: Unknown message type %u", msg.msg_type);
            break;
        }
        
        count++;
    }
    
    return count;
}

/**
 * Initialize the bridge system
 * @return 0 on success, -1 on failure
 */
int bridge_init(void)
{
    LOG_INFO("Initializing Wine bridge client...");
    
    // Try to connect
    if (bridge_connect() < 0) {
        LOG_INFO("Wine bridge not available (this is normal if not using Wine)");
        return 0;  // Not an error - bridge is optional
    }
    
    return 0;
}

/**
 * Cleanup bridge resources
 */
void bridge_cleanup(void)
{
    bridge_disconnect();
}
