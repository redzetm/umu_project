CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

# UmuOS の最小環境では動的リンカが無い/限られるため、静的リンク版が必要になります。
# 例: make -C uman STATIC=1
#     make -C uman STATIC=1 CC=musl-gcc
ifeq ($(STATIC),1)
	LDFLAGS += -static
	# CC が未指定なら musl-gcc を優先（無い場合は上書きされないので呼び出し側で指定してください）
	ifeq ($(origin CC), default)
		CC := musl-gcc
	endif
endif

TARGET := uman
SRC := src/uman.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)
