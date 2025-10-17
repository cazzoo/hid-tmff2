/*
 * T500RS Wine Bridge - UHID Proxy with IPC
 * 
 * Phase 2: Bidirectional communication between Wine and userspace driver
 * 
 * Data Flow:
 *   Input:  Real Device → Driver → Socket → Proxy → UHID → Wine
 *   Output: Wine → UHID → Proxy → Socket → Driver → Real Device
 * 
 * Usage: sudo ./uhid_proxy_ipc
 */

#include <linux/uhid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/tmp/t500rs_bridge.sock"
#define MAX_REPORT_SIZE 256

static int uhid_fd = -1;
static int socket_fd = -1;
static int driver_fd = -1;
static int running = 1;

// IPC message types
#define MSG_INPUT_REPORT  0x01
#define MSG_OUTPUT_REPORT 0x02
#define MSG_FF_REPORT     0x03

// IPC message structure
struct bridge_message {
    uint8_t msg_type;
    uint8_t report_id;
    uint16_t data_len;
    uint8_t data[MAX_REPORT_SIZE];
} __attribute__((packed));

void signal_handler(int sig) {
    printf("\nShutting down...\n");
    running = 0;
}

// HID descriptor for T500RS - matches raw USB report format
// Report ID 0x07: 15 bytes total
// Byte 0: Report ID (0x07)
// Bytes 1-2: Steering (0-65535, little-endian, center at 32768)
// Bytes 3-4: Brake (0-1023)
// Bytes 5-6: Throttle (0-1023) 
// Bytes 7-8: Clutch (0-1023)
// Bytes 9-10: Unknown
// Bytes 11-13: Buttons (24 bits)
// Byte 14: D-pad
static unsigned char hid_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x04,        // Usage (Joystick)
    0xA1, 0x01,        // Collection (Application)
    
    0x85, 0x07,        //   Report ID (7)
    
    // Steering axis (X) - 16-bit, 0-65535 (center at 32768)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X)
    0x15, 0x00,        //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  //   Logical Maximum (65535)
    0x35, 0x00,        //   Physical Minimum (0)
    0x47, 0xFF, 0xFF, 0x00, 0x00,  //   Physical Maximum (65535)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Brake (Z) - 16-bit, 0-1023
    0x09, 0x32,        //   Usage (Z)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0xFF, 0x03,  //   Physical Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Throttle (Y) - 16-bit, 0-1023
    0x09, 0x31,        //   Usage (Y)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0xFF, 0x03,  //   Physical Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Clutch (Rz) - 16-bit, 0-1023
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0xFF, 0x03,  //   Physical Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Unknown/Reserved - 16-bit
    0x09, 0x00,        //   Usage (Undefined)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0xFF,  //   Logical Maximum (65535)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Const,Var,Abs) - padding
    
    // Buttons (24 buttons, 3 bytes)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x18,        //   Usage Maximum (Button 24)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x18,        //   Report Count (24)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // D-pad (HAT switch) - 4-bit
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x39,        //   Usage (Hat Switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x55, 0x00,        //   Unit Exponent (0)
    0x65, 0x14,        //   Unit (Eng Rot: Degrees)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Padding (4 bits to complete byte)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Const,Var,Abs)
    
    // Force Feedback (HID PID) - Minimal implementation
    // This advertises basic FF capability to Wine
    0x05, 0x0F,        //   Usage Page (Physical Interface)
    0x09, 0x5A,        //   Usage (Set Effect Report)
    0xA1, 0x02,        //   Collection (Logical)
    0x85, 0x01,        //     Report ID (1) - Set Effect
    0x09, 0x22,        //     Usage (Effect Block Index)
    0x15, 0x01,        //     Logical Minimum (1)
    0x25, 0x28,        //     Logical Maximum (40)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x09, 0x25,        //     Usage (Effect Type)
    0xA1, 0x02,        //     Collection (Logical)
    0x09, 0x26,        //       Usage (ET Constant Force)
    0x09, 0x27,        //       Usage (ET Ramp)
    0x09, 0x30,        //       Usage (ET Square)
    0x09, 0x31,        //       Usage (ET Sine)
    0x09, 0x32,        //       Usage (ET Triangle)
    0x09, 0x33,        //       Usage (ET Sawtooth Up)
    0x09, 0x34,        //       Usage (ET Sawtooth Down)
    0x09, 0x40,        //       Usage (ET Spring)
    0x09, 0x41,        //       Usage (ET Damper)
    0x09, 0x42,        //       Usage (ET Inertia)
    0x09, 0x43,        //       Usage (ET Friction)
    0x25, 0x0B,        //       Logical Maximum (11)
    0x75, 0x08,        //       Report Size (8)
    0x95, 0x01,        //       Report Count (1)
    0x91, 0x00,        //       Output (Data,Array,Abs)
    0xC0,              //     End Collection
    0x09, 0x50,        //     Usage (Duration)
    0x09, 0x54,        //     Usage (Trigger Repeat Interval)
    0x09, 0x51,        //     Usage (Sample Period)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x03,        //     Report Count (3)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x09, 0x52,        //     Usage (Gain)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x09, 0x53,        //     Usage (Trigger Button)
    0x15, 0x01,        //     Logical Minimum (1)
    0x25, 0x08,        //     Logical Maximum (8)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x09, 0x55,        //     Usage (Axes Enable)
    0xA1, 0x02,        //     Collection (Logical)
    0x05, 0x01,        //       Usage Page (Generic Desktop)
    0x09, 0x30,        //       Usage (X)
    0x09, 0x31,        //       Usage (Y)
    0x15, 0x00,        //       Logical Minimum (0)
    0x25, 0x01,        //       Logical Maximum (1)
    0x75, 0x01,        //       Report Size (1)
    0x95, 0x02,        //       Report Count (2)
    0x91, 0x02,        //       Output (Data,Var,Abs)
    0xC0,              //     End Collection
    0x05, 0x0F,        //     Usage Page (Physical Interface)
    0x09, 0x56,        //     Usage (Direction Enable)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x95, 0x05,        //     Report Count (5) - padding
    0x91, 0x03,        //     Output (Const,Var,Abs)
    0x09, 0x57,        //     Usage (Direction)
    0xA1, 0x02,        //     Collection (Logical)
    0x0B, 0x01, 0x00, 0x0A, 0x00,  // Usage (Ordinals: Instance 1)
    0x0B, 0x02, 0x00, 0x0A, 0x00,  // Usage (Ordinals: Instance 2)
    0x66, 0x14, 0x00,  //       Unit (Eng Rot: Degrees)
    0x55, 0x00,        //       Unit Exponent (0)
    0x15, 0x00,        //       Logical Minimum (0)
    0x26, 0xB4, 0x00,  //       Logical Maximum (180)
    0x75, 0x08,        //       Report Size (8)
    0x95, 0x02,        //       Report Count (2)
    0x91, 0x02,        //       Output (Data,Var,Abs)
    0x55, 0x00,        //       Unit Exponent (0)
    0x65, 0x00,        //       Unit (None)
    0xC0,              //     End Collection
    0xC0,              //   End Collection
    
    // Effect Operation Report
    0x09, 0x5B,        //   Usage (Set Effect Operation Report)
    0xA1, 0x02,        //   Collection (Logical)
    0x85, 0x02,        //     Report ID (2) - Effect Operation
    0x09, 0x22,        //     Usage (Effect Block Index)
    0x15, 0x01,        //     Logical Minimum (1)
    0x25, 0x28,        //     Logical Maximum (40)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x09, 0x78,        //     Usage (Effect Operation)
    0xA1, 0x02,        //     Collection (Logical)
    0x09, 0x79,        //       Usage (Op Effect Start)
    0x09, 0x7A,        //       Usage (Op Effect Start Solo)
    0x09, 0x7B,        //       Usage (Op Effect Stop)
    0x15, 0x01,        //       Logical Minimum (1)
    0x25, 0x03,        //       Logical Maximum (3)
    0x91, 0x00,        //       Output (Data,Array,Abs)
    0xC0,              //     End Collection
    0x09, 0x7C,        //     Usage (Loop Count)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0xC0,              //   End Collection
    
    0xC0               // End Collection (Application)
};

int uhid_create(void) {
    uhid_fd = open("/dev/uhid", O_RDWR | O_CLOEXEC);
    if (uhid_fd < 0) {
        perror("Cannot open /dev/uhid");
        printf("\nMake sure UHID module is loaded: sudo modprobe uhid\n");
        return -1;
    }
    
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_CREATE2;
    
    strcpy((char*)ev.u.create2.name, "T500RS Wine Bridge (Input Only)");
    memcpy(ev.u.create2.rd_data, hid_descriptor, sizeof(hid_descriptor));
    ev.u.create2.rd_size = sizeof(hid_descriptor);
    ev.u.create2.bus = BUS_USB;
    ev.u.create2.vendor = 0x044F;
    ev.u.create2.product = 0xB65E;
    ev.u.create2.version = 0x0100;
    ev.u.create2.country = 0;
    
    if (write(uhid_fd, &ev, sizeof(ev)) < 0) {
        perror("Cannot create UHID device");
        close(uhid_fd);
        return -1;
    }
    
    printf("✓ UHID device created successfully!\n");
    printf("  VID:PID = 044f:b65e\n");
    printf("  Name: T500RS Wine Bridge (Input Only)\n");
    printf("\n");
    printf("NOTE: This device provides INPUT ONLY (wheel/pedals/buttons)\n");
    printf("      For FORCE FEEDBACK, use the real T500RS device in Wine:\n");
    printf("      'T500RS Force Feedback Wheel' (not 'Wine Bridge')\n");
    
    return 0;
}

void uhid_destroy(void) {
    if (uhid_fd < 0)
        return;
    
    struct uhid_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = UHID_DESTROY;
    write(uhid_fd, &ev, sizeof(ev));
    
    close(uhid_fd);
    uhid_fd = -1;
}

int socket_server_init(void) {
    socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_fd < 0) {
        perror("Cannot create socket");
        return -1;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    unlink(SOCKET_PATH);
    
    if (bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Cannot bind socket");
        close(socket_fd);
        return -1;
    }
    
    if (listen(socket_fd, 1) < 0) {
        perror("Cannot listen on socket");
        close(socket_fd);
        return -1;
    }
    
    printf("✓ IPC server listening on %s\n", SOCKET_PATH);
    return 0;
}

int handle_uhid_event(void) {
    struct uhid_event ev;
    ssize_t ret;
    
    ret = read(uhid_fd, &ev, sizeof(ev));
    if (ret < 0) {
        if (errno == EAGAIN)
            return 0;
        perror("UHID read error");
        return -1;
    }
    
    if (ret != sizeof(ev)) {
        fprintf(stderr, "Invalid UHID event size\n");
        return -1;
    }
    
    switch (ev.type) {
    case UHID_START:
        printf("UHID: Device started\n");
        break;
        
    case UHID_STOP:
        printf("UHID: Device stopped\n");
        break;
        
    case UHID_OPEN:
        printf("UHID: Device opened by application\n");
        break;
        
    case UHID_CLOSE:
        printf("UHID: Device closed by application\n");
        break;
        
    case UHID_OUTPUT:
        printf("UHID: Output report received (%u bytes)\n", ev.u.output.size);
        // Forward to driver
        if (driver_fd >= 0) {
            struct bridge_message msg;
            msg.msg_type = MSG_OUTPUT_REPORT;
            msg.report_id = ev.u.output.rtype;
            msg.data_len = ev.u.output.size;
            memcpy(msg.data, ev.u.output.data, ev.u.output.size);
            send(driver_fd, &msg, sizeof(msg.msg_type) + sizeof(msg.report_id) + 
                 sizeof(msg.data_len) + msg.data_len, MSG_DONTWAIT);
        }
        break;
        
    case UHID_SET_REPORT:
        printf("UHID: Set report received (type=%u, id=%u, %u bytes)\n",
               ev.u.set_report.rtype, ev.u.set_report.id, ev.u.set_report.size);
        // Forward to driver (force feedback)
        if (driver_fd >= 0) {
            struct bridge_message msg;
            msg.msg_type = MSG_FF_REPORT;
            msg.report_id = ev.u.set_report.id;
            msg.data_len = ev.u.set_report.size;
            memcpy(msg.data, ev.u.set_report.data, ev.u.set_report.size);
            send(driver_fd, &msg, sizeof(msg.msg_type) + sizeof(msg.report_id) + 
                 sizeof(msg.data_len) + msg.data_len, MSG_DONTWAIT);
        }
        
        // Send response
        struct uhid_event response;
        memset(&response, 0, sizeof(response));
        response.type = UHID_SET_REPORT_REPLY;
        response.u.set_report_reply.id = ev.u.set_report.id;
        response.u.set_report_reply.err = 0;
        write(uhid_fd, &response, sizeof(response));
        break;
        
    case UHID_GET_REPORT:
        printf("UHID: Get report requested\n");
        // Send empty response
        memset(&ev, 0, sizeof(ev));
        ev.type = UHID_GET_REPORT_REPLY;
        ev.u.get_report_reply.id = 0;
        ev.u.get_report_reply.err = 0;
        ev.u.get_report_reply.size = 0;
        write(uhid_fd, &ev, sizeof(ev));
        break;
        
    default:
        printf("UHID: Unknown event type %u\n", ev.type);
        break;
    }
    
    return 0;
}

int handle_driver_message(void) {
    struct bridge_message msg;
    ssize_t ret;
    
    ret = recv(driver_fd, &msg, sizeof(msg), MSG_DONTWAIT);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        perror("Driver socket read error");
        return -1;
    }
    
    if (ret == 0) {
        printf("Driver disconnected\n");
        close(driver_fd);
        driver_fd = -1;
        return 0;
    }
    
    // Handle message based on type
    switch (msg.msg_type) {
    case MSG_INPUT_REPORT:
        // Forward to UHID
        if (msg.data_len > 0 && msg.data_len <= MAX_REPORT_SIZE) {
            struct uhid_event ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = UHID_INPUT2;
            ev.u.input2.size = msg.data_len;
            memcpy(ev.u.input2.data, msg.data, msg.data_len);
            
            if (write(uhid_fd, &ev, sizeof(ev)) < 0) {
                perror("Failed to send input to UHID");
            }
        }
        break;
        
    default:
        printf("Unknown message type from driver: %u\n", msg.msg_type);
        break;
    }
    
    return 0;
}

int main(void) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    printf("T500RS Wine Bridge - UHID Proxy with IPC\n");
    printf("==========================================\n\n");
    
    // Create UHID device
    if (uhid_create() < 0) {
        return 1;
    }
    
    // Create IPC server
    if (socket_server_init() < 0) {
        uhid_destroy();
        return 1;
    }
    
    printf("\n");
    printf("✓ Bridge ready!\n");
    printf("  UHID device: /dev/input/eventX (check dmesg)\n");
    printf("  IPC socket: %s\n", SOCKET_PATH);
    printf("\n");
    printf("Waiting for userspace driver to connect...\n");
    printf("  Start with: sudo ./userspace/t500rs-ffb-modular\n");
    printf("\n");
    printf("Press Ctrl+C to stop.\n\n");
    
    // Main event loop
    struct pollfd fds[3];
    int nfds;
    
    while (running) {
        nfds = 0;
        
        // Poll UHID device
        fds[nfds].fd = uhid_fd;
        fds[nfds].events = POLLIN;
        nfds++;
        
        // Poll server socket for new connections
        if (driver_fd < 0) {
            fds[nfds].fd = socket_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        } else {
            // Poll driver connection
            fds[nfds].fd = driver_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }
        
        int ret = poll(fds, nfds, 1000);
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            perror("Poll error");
            break;
        }
        
        if (ret == 0)
            continue;
        
        // Check UHID events
        if (fds[0].revents & POLLIN) {
            if (handle_uhid_event() < 0) {
                break;
            }
        }
        
        // Check for new driver connection
        if (driver_fd < 0 && nfds > 1 && (fds[1].revents & POLLIN)) {
            driver_fd = accept(socket_fd, NULL, NULL);
            if (driver_fd >= 0) {
                // Set non-blocking
                int flags = fcntl(driver_fd, F_GETFL, 0);
                fcntl(driver_fd, F_SETFL, flags | O_NONBLOCK);
                printf("✓ Userspace driver connected!\n");
                printf("  Input and force feedback should now work in Wine/Proton games.\n\n");
            }
        }
        
        // Check driver messages
        if (driver_fd >= 0 && nfds > 1 && (fds[1].revents & POLLIN)) {
            if (handle_driver_message() < 0) {
                // Driver disconnected or error
                if (driver_fd >= 0) {
                    close(driver_fd);
                    driver_fd = -1;
                }
            }
        }
    }
    
    // Cleanup
    if (driver_fd >= 0)
        close(driver_fd);
    if (socket_fd >= 0) {
        close(socket_fd);
        unlink(SOCKET_PATH);
    }
    uhid_destroy();
    
    printf("Bridge stopped.\n");
    return 0;
}
