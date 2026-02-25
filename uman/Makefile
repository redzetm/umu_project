CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic

TARGET := uman
SRC := src/uman.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

clean:
	rm -f $(TARGET)
