# Makefile for T500RS Test Programs

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm

# Test programs
C_DIR = c
TESTS = $(C_DIR)/test_all_effects \
        $(C_DIR)/test_direction \
        $(C_DIR)/test_envelope \
        $(C_DIR)/test_input_reading \
        $(C_DIR)/test_multi_effect

# Default target
all: $(TESTS)
	@echo "All test programs built successfully"

# Build individual tests
$(C_DIR)/test_all_effects: $(C_DIR)/test_all_effects.c
	@echo "Building test_all_effects..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(C_DIR)/test_direction: $(C_DIR)/test_direction.c
	@echo "Building test_direction..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(C_DIR)/test_envelope: $(C_DIR)/test_envelope.c
	@echo "Building test_envelope..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(C_DIR)/test_input_reading: $(C_DIR)/test_input_reading.c
	@echo "Building test_input_reading..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(C_DIR)/test_multi_effect: $(C_DIR)/test_multi_effect.c
	@echo "Building test_multi_effect..."
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# Clean
clean:
	@echo "Cleaning test programs..."
	rm -f $(TESTS)
	@echo "Clean complete"

# Help
help:
	@echo "T500RS Test Programs"
	@echo ""
	@echo "Targets:"
	@echo "  all                  - Build all test programs (default)"
	@echo "  clean                - Remove compiled test programs"
	@echo "  help                 - Show this help message"
	@echo ""
	@echo "Test Programs:"
	@echo "  test_all_effects     - Comprehensive effect testing"
	@echo "  test_direction       - Direction testing"
	@echo "  test_envelope        - Envelope (attack/fade) testing"
	@echo "  test_input_reading   - Input reading test"
	@echo "  test_multi_effect    - Multi-effect mixing test"
	@echo ""
	@echo "Python Tests:"
	@echo "  See python/ directory for Python test scripts"
	@echo ""
	@echo "Utility Scripts:"
	@echo "  See scripts/ directory for utility scripts"

.PHONY: all clean help

