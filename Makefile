CC = /opt/compiler/bin/clang
WARNFLAGS = -Wall -Wextra -Wpedantic
# Native tuning and ThinLTO maximize throughput on the machine that builds rx.
# Override OPTFLAGS (for example, `make OPTFLAGS='-O2'`) for portable builds.
OPTFLAGS ?= -O3 -DNDEBUG -march=native -mtune=native -flto=thin -fomit-frame-pointer -ffunction-sections -fdata-sections

# Profile-Guided Optimization (PGO) support
# Usage: make clean && make PGO_GEN=1 && LLVM_PROFILE_FILE=rx.profraw ./rx < workload && make clean && make PGO_USE=1
PGO_GEN ?= 0
PGO_USE ?= 0
ifeq ($(PGO_GEN),1)
    OPTFLAGS += -fprofile-generate
endif
ifeq ($(PGO_USE),1)
    OPTFLAGS += -fprofile-use=rx.profdata
endif

CPPFLAGS = -Iinclude $(shell pcre2-config --cflags)
CFLAGS ?= $(WARNFLAGS) $(OPTFLAGS) -std=c17
LDFLAGS ?= -flto=thin -Wl,-O2 -Wl,--gc-sections
LDLIBS = $(shell pcre2-config --libs8)

# FIXED: Added src/replace.c and src/simd.c to the compilation list
SRCS = src/string_view.c src/match.c src/replace.c src/aggregate.c src/pipeline.c src/main.c src/simd.c
OBJS = $(SRCS:.c=.o)
TARGET = rx

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET) tests/*.o tests/test_basic *.profraw

test: tests/test_basic
	./tests/test_basic

tests/test_basic: tests/test_basic.c src/string_view.c src/match.c src/replace.c src/aggregate.c src/pipeline.c src/simd.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG $(LDFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: all clean test
