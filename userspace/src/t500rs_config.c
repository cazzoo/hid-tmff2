/**
 * @file t500rs_config.c
 * @brief Configuration management implementation
 */

#include "t500rs_config.h"
#include "t500rs_logging.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Global configuration instance */
driver_config_t g_config;

/**
 * @brief Get default driver configuration
 */
driver_config_t get_default_config(void)
{
    driver_config_t config = {
        .ffb = {
            .update_rate_hz = 50,
            .skip_identical_updates = true,
            .default_gain = 65535,
            .ignore_zero_gain = true,
            .autocenter_effect_id = 15,
            .min_autocenter_strength = 10,
            .force_thread_priority = 0,
            .input_thread_priority = 0,
        },
        .usb = {
            .timeout_ms = 1000,
            .max_retries = 3,
            .hex_debug = false,
        },
        .log = {
            .level = LOG_LEVEL_INFO,
            .use_colors = true,
            .show_timestamps = false,
            .log_file = NULL,
        },
    };
    
    return config;
}

/**
 * @brief Parse a configuration line
 */
static int parse_config_line(const char *line, driver_config_t *config)
{
    char key[128], value[128];
    
    /* Skip comments and empty lines */
    if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') {
        return 0;
    }
    
    /* Parse key=value */
    if (sscanf(line, "%127[^=]=%127s", key, value) != 2) {
        return 0;  /* Skip malformed lines */
    }
    
    /* Trim whitespace */
    char *k = key;
    while (*k == ' ' || *k == '\t') k++;
    char *v = value;
    while (*v == ' ' || *v == '\t') v++;
    
    /* Parse FFB settings */
    if (strcmp(k, "update_rate_hz") == 0) {
        config->ffb.update_rate_hz = atoi(v);
    } else if (strcmp(k, "skip_identical_updates") == 0) {
        config->ffb.skip_identical_updates = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
    } else if (strcmp(k, "default_gain") == 0) {
        config->ffb.default_gain = (uint16_t)atoi(v);
    } else if (strcmp(k, "ignore_zero_gain") == 0) {
        config->ffb.ignore_zero_gain = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
    } else if (strcmp(k, "autocenter_effect_id") == 0) {
        config->ffb.autocenter_effect_id = atoi(v);
    } else if (strcmp(k, "min_autocenter_strength") == 0) {
        config->ffb.min_autocenter_strength = atoi(v);
    }
    /* Parse USB settings */
    else if (strcmp(k, "usb_timeout_ms") == 0) {
        config->usb.timeout_ms = atoi(v);
    } else if (strcmp(k, "usb_max_retries") == 0) {
        config->usb.max_retries = atoi(v);
    } else if (strcmp(k, "usb_hex_debug") == 0) {
        config->usb.hex_debug = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
    }
    /* Parse logging settings */
    else if (strcmp(k, "log_level") == 0) {
        if (strcmp(v, "error") == 0) config->log.level = LOG_LEVEL_ERROR;
        else if (strcmp(v, "warn") == 0) config->log.level = LOG_LEVEL_WARN;
        else if (strcmp(v, "info") == 0) config->log.level = LOG_LEVEL_INFO;
        else if (strcmp(v, "debug") == 0) config->log.level = LOG_LEVEL_DEBUG;
    } else if (strcmp(k, "log_use_colors") == 0) {
        config->log.use_colors = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
    } else if (strcmp(k, "log_show_timestamps") == 0) {
        config->log.show_timestamps = (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
    }
    
    return 0;
}

/**
 * @brief Load configuration from file
 */
int load_config_from_file(const char *filename, driver_config_t *config)
{
    FILE *f = fopen(filename, "r");
    if (!f) {
        return -1;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        parse_config_line(line, config);
    }
    
    fclose(f);
    return 0;
}

/**
 * @brief Save configuration to file
 */
int save_config_to_file(const char *filename, const driver_config_t *config)
{
    FILE *f = fopen(filename, "w");
    if (!f) {
        return -1;
    }
    
    fprintf(f, "# T500RS Driver Configuration\n\n");
    
    fprintf(f, "# Force Feedback Settings\n");
    fprintf(f, "update_rate_hz=%d\n", config->ffb.update_rate_hz);
    fprintf(f, "skip_identical_updates=%s\n", config->ffb.skip_identical_updates ? "true" : "false");
    fprintf(f, "default_gain=%u\n", config->ffb.default_gain);
    fprintf(f, "ignore_zero_gain=%s\n", config->ffb.ignore_zero_gain ? "true" : "false");
    fprintf(f, "autocenter_effect_id=%d\n", config->ffb.autocenter_effect_id);
    fprintf(f, "min_autocenter_strength=%d\n\n", config->ffb.min_autocenter_strength);
    
    fprintf(f, "# USB Settings\n");
    fprintf(f, "usb_timeout_ms=%d\n", config->usb.timeout_ms);
    fprintf(f, "usb_max_retries=%d\n", config->usb.max_retries);
    fprintf(f, "usb_hex_debug=%s\n\n", config->usb.hex_debug ? "true" : "false");
    
    fprintf(f, "# Logging Settings\n");
    const char *level_str = "info";
    switch (config->log.level) {
        case LOG_LEVEL_ERROR: level_str = "error"; break;
        case LOG_LEVEL_WARN: level_str = "warn"; break;
        case LOG_LEVEL_INFO: level_str = "info"; break;
        case LOG_LEVEL_DEBUG: level_str = "debug"; break;
    }
    fprintf(f, "log_level=%s\n", level_str);
    fprintf(f, "log_use_colors=%s\n", config->log.use_colors ? "true" : "false");
    fprintf(f, "log_show_timestamps=%s\n", config->log.show_timestamps ? "true" : "false");
    
    fclose(f);
    return 0;
}

/**
 * @brief Print current configuration
 */
void print_config(const driver_config_t *config)
{
    printf("\n========================================\n");
    printf("T500RS Driver Configuration\n");
    printf("========================================\n\n");
    
    printf("Force Feedback:\n");
    printf("  Update rate: %d Hz\n", config->ffb.update_rate_hz);
    printf("  Skip identical updates: %s\n", config->ffb.skip_identical_updates ? "Yes" : "No");
    printf("  Default gain: %u (%.1f%%)\n", config->ffb.default_gain, 
           config->ffb.default_gain * 100.0 / 65535.0);
    printf("  Ignore zero gain: %s\n", config->ffb.ignore_zero_gain ? "Yes" : "No");
    printf("  Autocenter effect ID: %d\n", config->ffb.autocenter_effect_id);
    printf("  Min autocenter strength: %d\n\n", config->ffb.min_autocenter_strength);
    
    printf("USB:\n");
    printf("  Timeout: %d ms\n", config->usb.timeout_ms);
    printf("  Max retries: %d\n", config->usb.max_retries);
    printf("  Hex debug: %s\n\n", config->usb.hex_debug ? "Enabled" : "Disabled");
    
    printf("Logging:\n");
    const char *level_str = "Unknown";
    switch (config->log.level) {
        case LOG_LEVEL_ERROR: level_str = "Error"; break;
        case LOG_LEVEL_WARN: level_str = "Warning"; break;
        case LOG_LEVEL_INFO: level_str = "Info"; break;
        case LOG_LEVEL_DEBUG: level_str = "Debug"; break;
    }
    printf("  Level: %s\n", level_str);
    printf("  Use colors: %s\n", config->log.use_colors ? "Yes" : "No");
    printf("  Show timestamps: %s\n", config->log.show_timestamps ? "Yes" : "No");
    printf("  Log file: %s\n", config->log.log_file ? config->log.log_file : "None (stdout)");
    
    printf("\n========================================\n\n");
}

