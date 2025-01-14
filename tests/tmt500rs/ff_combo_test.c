#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

#define MAX_EFFECTS 4
#define TEST_DURATION 10 // seconds per test
#define EFFECT_DELAY 100000 // 100ms between effects

struct ff_effect effects[MAX_EFFECTS];
int effect_ids[MAX_EFFECTS];

// Helper function to upload an effect
int upload_effect(int fd, struct ff_effect *effect) {
    effect_ids[effect->id] = effect->id;
    if (ioctl(fd, EVIOCSFF, effect) == -1) {
        printf("Error uploading effect: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// Helper function to play an effect
int play_effect(int fd, int effect_id, int value) {
    struct input_event play;
    play.type = EV_FF;
    play.code = effect_id;
    play.value = value;
    
    if (write(fd, (const void*) &play, sizeof(play)) == -1) {
        printf("Error playing effect: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

// Test constant force effect
void setup_constant_force(struct ff_effect *effect, int id, int level) {
    memset(effect, 0, sizeof(struct ff_effect));
    effect->type = FF_CONSTANT;
    effect->id = id;
    effect->trigger.button = 0;
    effect->trigger.interval = 0;
    effect->replay.length = 1000;  // 1 second
    effect->replay.delay = 0;
    effect->u.constant.level = level;
    effect->direction = 0x4000; // 90 degrees
}

// Test spring effect
void setup_spring(struct ff_effect *effect, int id, int stiffness) {
    memset(effect, 0, sizeof(struct ff_effect));
    effect->type = FF_SPRING;
    effect->id = id;
    effect->trigger.button = 0;
    effect->trigger.interval = 0;
    effect->replay.length = 1000;
    effect->replay.delay = 0;
    effect->u.condition[0].right_saturation = 0x3fff;  // Reduced from 0x7fff
    effect->u.condition[0].left_saturation = 0x3fff;   // Reduced from 0x7fff
    effect->u.condition[0].right_coeff = stiffness;
    effect->u.condition[0].left_coeff = stiffness;
    effect->u.condition[0].deadband = 0;
    effect->u.condition[0].center = 0;
}

// Test damper effect
void setup_damper(struct ff_effect *effect, int id, int damping) {
    memset(effect, 0, sizeof(struct ff_effect));
    effect->type = FF_DAMPER;
    effect->id = id;
    effect->trigger.button = 0;
    effect->trigger.interval = 0;
    effect->replay.length = 1000;
    effect->replay.delay = 0;
    effect->u.condition[0].right_saturation = 0x3fff;  // Reduced from 0x7fff
    effect->u.condition[0].left_saturation = 0x3fff;   // Reduced from 0x7fff
    effect->u.condition[0].right_coeff = damping;
    effect->u.condition[0].left_coeff = damping;
    effect->u.condition[0].deadband = 0;
    effect->u.condition[0].center = 0;
}

// Test periodic effect
void setup_periodic(struct ff_effect *effect, int id, __u16 waveform, __s16 magnitude) {
    memset(effect, 0, sizeof(struct ff_effect));
    effect->type = FF_PERIODIC;
    effect->id = id;
    effect->trigger.button = 0;
    effect->trigger.interval = 0;
    effect->replay.length = 1000;
    effect->replay.delay = 0;
    effect->u.periodic.waveform = waveform;
    effect->u.periodic.period = 100;     // 100ms
    effect->u.periodic.magnitude = magnitude;
    effect->u.periodic.offset = 0;
    effect->u.periodic.phase = 0;
    effect->direction = 0x4000;  // 90 degrees
}

// Test combination of effects
void test_effect_combination(int fd) {
    printf("\nTesting effect combinations...\n");
    
    // Setup effects with reduced magnitudes
    setup_constant_force(&effects[0], 0, 0x2000);  // 25% force
    setup_spring(&effects[1], 1, 0x2000);          // Light stiffness
    setup_damper(&effects[2], 2, 0x2000);          // Light damping
    setup_periodic(&effects[3], 3, FF_SINE, 0x2000); // Sine wave at 25%
    
    // Upload all effects
    for (int i = 0; i < MAX_EFFECTS; i++) {
        if (upload_effect(fd, &effects[i]) < 0) {
            printf("Failed to upload effect %d\n", i);
            return;
        }
    }
    
    printf("Testing individual effects...\n");
    // Test each effect individually
    for (int i = 0; i < MAX_EFFECTS; i++) {
        printf("Playing effect %d...\n", i);
        play_effect(fd, effect_ids[i], 1);
        usleep(TEST_DURATION * 1000000);
        play_effect(fd, effect_ids[i], 0);
        usleep(EFFECT_DELAY);
    }
    
    printf("Testing effect pairs...\n");
    // Test pairs of effects
    for (int i = 0; i < MAX_EFFECTS; i++) {
        for (int j = i + 1; j < MAX_EFFECTS; j++) {
            printf("Playing effects %d and %d...\n", i, j);
            play_effect(fd, effect_ids[i], 1);
            play_effect(fd, effect_ids[j], 1);
            usleep(TEST_DURATION * 1000000);
            play_effect(fd, effect_ids[i], 0);
            play_effect(fd, effect_ids[j], 0);
            usleep(EFFECT_DELAY);
        }
    }
    
    printf("Testing all effects together...\n");
    // Test all effects together
    for (int i = 0; i < MAX_EFFECTS; i++) {
        play_effect(fd, effect_ids[i], 1);
    }
    usleep(TEST_DURATION * 1000000);
    for (int i = 0; i < MAX_EFFECTS; i++) {
        play_effect(fd, effect_ids[i], 0);
    }
    
    // Remove effects
    struct ff_effect temp;
    for (int i = 0; i < MAX_EFFECTS; i++) {
        temp = effects[i];
        temp.id = effect_ids[i];
        if (ioctl(fd, EVIOCRMFF, temp.id) == -1) {
            printf("Error removing effect %d: %s\n", i, strerror(errno));
        }
    }
}

// Performance test
void test_performance(int fd) {
    printf("\nRunning performance test...\n");
    struct timespec start, end;
    long latency;
    
    // Setup a simple constant force effect
    setup_constant_force(&effects[0], 0, 0x4000);
    if (upload_effect(fd, &effects[0]) < 0) {
        printf("Failed to upload effect for performance test\n");
        return;
    }
    
    // Measure latency
    printf("Measuring effect latency...\n");
    for (int i = 0; i < 10; i++) {
        clock_gettime(CLOCK_MONOTONIC, &start);
        play_effect(fd, effect_ids[0], 1);
        clock_gettime(CLOCK_MONOTONIC, &end);
        
        latency = (end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec);
        printf("Iteration %d latency: %ld ns\n", i, latency);
        
        usleep(100000);  // 100ms delay
        play_effect(fd, effect_ids[0], 0);
        usleep(100000);
    }
    
    // Cleanup
    struct ff_effect temp = effects[0];
    temp.id = effect_ids[0];
    if (ioctl(fd, EVIOCRMFF, temp.id) == -1) {
        printf("Error removing effect: %s\n", strerror(errno));
    }
}

int main(int argc, char **argv) {
    int fd;
    
    if (argc < 2) {
        printf("Usage: %s <device>\n", argv[0]);
        printf("Example: %s /dev/input/event0\n", argv[0]);
        exit(1);
    }
    
    if ((fd = open(argv[1], O_RDWR)) == -1) {
        printf("Could not open %s: %s\n", argv[1], strerror(errno));
        exit(1);
    }
    
    printf("TMT500RS Force Feedback Hardware Validation\n");
    printf("==========================================\n");
    
    // Run tests
    test_effect_combination(fd);
    test_performance(fd);
    
    close(fd);
    printf("\nHardware validation complete\n");
    return 0;
} 