/**
 * @file t500rs_logging.h
 * @brief Logging system for T500RS driver
 * 
 * Provides a flexible logging system with multiple levels,
 * color support, and optional timestamps.
 */

#ifndef T500RS_LOGGING_H
#define T500RS_LOGGING_H

#include <stdio.h>
#include <time.h>
#include "t500rs_config.h"  /* For log_level_t */

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[0;31m"
#define COLOR_YELLOW  "\033[0;33m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_CYAN    "\033[0;36m"
#define COLOR_GRAY    "\033[0;90m"

/**
 * @brief Initialize logging system
 * @param level Initial log level
 * @param use_colors Enable color output
 * @param show_timestamps Show timestamps in logs
 */
void logging_init(log_level_t level, int use_colors, int show_timestamps);

/**
 * @brief Set log level
 * @param level New log level
 */
void logging_set_level(log_level_t level);

/**
 * @brief Get current log level
 * @return Current log level
 */
log_level_t logging_get_level(void);

/**
 * @brief Log a message with specified level
 * @param level Log level
 * @param format Printf-style format string
 * @param ... Format arguments
 */
void log_message(log_level_t level, const char *format, ...);

/* Convenience macros for different log levels */
#define LOG_ERROR(fmt, ...) log_message(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  log_message(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  log_message(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) log_message(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

#endif /* T500RS_LOGGING_H */

