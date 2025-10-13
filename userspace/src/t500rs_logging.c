/**
 * @file t500rs_logging.c
 * @brief Logging system implementation
 */

#include "t500rs_logging.h"
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>

/* Global logging state */
static log_level_t current_log_level = LOG_LEVEL_INFO;
static int use_colors = 1;
static int show_timestamps = 0;
static FILE *log_file = NULL;

/**
 * @brief Initialize logging system
 */
void logging_init(log_level_t level, int colors, int timestamps)
{
    current_log_level = level;
    use_colors = colors;
    show_timestamps = timestamps;
    log_file = NULL;  /* Could be extended to support file logging */
}

/**
 * @brief Set log level
 */
void logging_set_level(log_level_t level)
{
    current_log_level = level;
}

/**
 * @brief Get current log level
 */
log_level_t logging_get_level(void)
{
    return current_log_level;
}

/**
 * @brief Get timestamp string
 */
static void get_timestamp(char *buf, size_t size)
{
    struct timeval tv;
    struct tm *tm_info;
    
    gettimeofday(&tv, NULL);
    tm_info = localtime(&tv.tv_sec);
    
    snprintf(buf, size, "%02d:%02d:%02d.%03ld",
             tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
             tv.tv_usec / 1000);
}

/**
 * @brief Log a message with specified level
 */
void log_message(log_level_t level, const char *format, ...)
{
    /* Check if message should be logged */
    if (level > current_log_level) {
        return;
    }
    
    /* Determine output stream */
    FILE *out = (level == LOG_LEVEL_ERROR) ? stderr : stdout;
    
    /* Get level string and color */
    const char *level_str;
    const char *color = "";
    const char *reset = "";
    
    if (use_colors) {
        reset = COLOR_RESET;
    }
    
    switch (level) {
        case LOG_LEVEL_ERROR:
            level_str = "ERROR";
            if (use_colors) color = COLOR_RED;
            break;
        case LOG_LEVEL_WARN:
            level_str = "WARN ";
            if (use_colors) color = COLOR_YELLOW;
            break;
        case LOG_LEVEL_INFO:
            level_str = "INFO ";
            if (use_colors) color = COLOR_GREEN;
            break;
        case LOG_LEVEL_DEBUG:
            level_str = "DEBUG";
            if (use_colors) color = COLOR_GRAY;
            break;
        default:
            level_str = "?????";
            break;
    }
    
    /* Print timestamp if enabled */
    if (show_timestamps) {
        char timestamp[32];
        get_timestamp(timestamp, sizeof(timestamp));
        fprintf(out, "%s[%s]%s ", COLOR_CYAN, timestamp, reset);
    }
    
    /* Print level */
    fprintf(out, "%s[%s]%s ", color, level_str, reset);
    
    /* Print message */
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);
    
    fprintf(out, "\n");
    fflush(out);
    
    /* Also log to file if configured */
    if (log_file) {
        if (show_timestamps) {
            char timestamp[32];
            get_timestamp(timestamp, sizeof(timestamp));
            fprintf(log_file, "[%s] ", timestamp);
        }
        fprintf(log_file, "[%s] ", level_str);
        va_start(args, format);
        vfprintf(log_file, format, args);
        va_end(args);
        fprintf(log_file, "\n");
        fflush(log_file);
    }
}

