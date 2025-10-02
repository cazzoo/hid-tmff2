# Makefile for T500RS Force Feedback Userspace Driver

CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LDFLAGS = -lusb-1.0 -pthread

TARGET = t500rs-ffb
SRC = t500rs-ffb.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/local/bin/$(TARGET)
	@echo "Installed to /usr/local/bin/$(TARGET)"
	@echo "Run with: sudo t500rs-ffb"

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall

