TARGET = aesdsocket

# CROSS_COMPILE ?= aarch64-none-linux-gnu-
CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -Wextra -pthread -g -O2
LDFLAGS = -pthread

SRC_DIR = src
OBJ_DIR = build

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(TARGET) $(OBJ_DIR)

re: clean all

.PHONY: all clean re
