# Makefile for wb_modbus_bridge Linux PoC
# Requires: libmosquitto-dev, cJSON (fetched below)
#
# Build:   make
# Clean:   make clean
# Fetch deps: make deps

CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -O2 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -Isrc -Ithirdparty/cJSON
LDFLAGS ?= -lmosquitto -lm

SRCS = src/bridge.c src/template.c src/modbus_rtu.c src/modbus_frame.c \
       src/mqtt_client.c src/value_conv.c thirdparty/cJSON/cJSON.c

OBJS = $(SRCS:.c=.o)

TARGET = wb_bridge

.PHONY: all clean deps

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Fetch cJSON from GitHub (single .c/.h pair, MIT license)
deps:
	mkdir -p thirdparty/cJSON
	curl -sSL https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c \
	     -o thirdparty/cJSON/cJSON.c
	curl -sSL https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h \
	     -o thirdparty/cJSON/cJSON.h
	# Remove our stub header so the real one is used
	rm -f src/cJSON.h

clean:
	rm -f $(OBJS) $(TARGET)
