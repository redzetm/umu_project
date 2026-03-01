CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=

TARGET := su
SRC := src/su.c

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

# crypt() は環境によって次の差がある:
# - 追加ライブラリ無しでリンクできる
# - libcrypt が必要
# - libxcrypt が必要
# そのため、(no extra) -> -lcrypt -> -lxcrypt の順で試す。
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) || \
		$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) -lcrypt || \
		$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS) -lxcrypt

clean:
	rm -f $(TARGET)
