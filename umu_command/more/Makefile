CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

TARGET := more
SRC := src/more.c

# UmuOS へ持ち込む場合は静的リンク推奨
# 例: make STATIC=1 CC=musl-gcc
ifeq ($(STATIC),1)
  LDFLAGS += -static
  ifeq ($(origin CC), default)
    CC := musl-gcc
  endif
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
