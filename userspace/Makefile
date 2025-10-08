# Makefile for T500RS Force Feedback Userspace Driver
# Enhanced version with Windows-compatible protocol support

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread -DUSE_WINDOWS_PROTOCOL=0
LDFLAGS = -lusb-1.0 -pthread

TARGET = t500rs-ffb
SOURCES = t500rs-ffb.c t500rs_protocol.c t500rs_effects.c
OBJECTS = $(SOURCES:.c=.o)
TESTS = test_all_effects

# Header dependencies
DEPS = t500rs_protocol.h

all: $(TARGET) $(TESTS)

# Build object files
%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

# Build main target from object files
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

test_all_effects: test_all_effects.c
	$(CC) $(CFLAGS) -o test_all_effects test_all_effects.c $(LDFLAGS)

clean:
	rm -f $(TARGET) $(TESTS) $(OBJECTS)

install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"
	@echo "Run with: sudo t500rs-ffb"

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall

