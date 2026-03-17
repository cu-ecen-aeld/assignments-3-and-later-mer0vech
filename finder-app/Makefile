TARGET = writer

CROSS_COMPILE ?=

CC = $(CROSS_COMPILE)gcc

CFLAGS = -Wall -02

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $^ -o $@

%.o: %.c
	$(CC) -c $< -o $@

.PHONY: clean

clean:
	rm -rf $(TARGET) $(OBJS)
