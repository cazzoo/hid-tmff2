# Makefile for T500RS Force Feedback Userspace Driver

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -lusb-1.0 -pthread

TARGET = t500rs-ffb
SRC = t500rs-ffb.c
TESTS = test_all_effects test_gain_autocenter

all: $(TARGET) $(TESTS)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

test_all_effects: test_all_effects.c
	$(CC) $(CFLAGS) -o test_all_effects test_all_effects.c $(LDFLAGS)

test_gain_autocenter: test_gain_autocenter.c
	$(CC) $(CFLAGS) -o test_gain_autocenter test_gain_autocenter.c

clean:
	rm -f $(TARGET) $(TESTS)

install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"
	@echo "Run with: sudo t500rs-ffb"

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall

