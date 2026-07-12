CC = /opt/compiler/bin/clang
WARNFLAGS = -Wall -Wextra -Wpedantic
# Native tuning and ThinLTO maximize throughput on the machine that builds rx.
# Override OPTFLAGS (for example, `make OPTFLAGS='-O2'`) for portable builds.
OPTFLAGS ?= -O3 -DNDEBUG -march=native -mtune=native -flto=thin -fomit-frame-pointer -ffunction-sections -fdata-sections
CPPFLAGS = -Iinclude $(shell pcre2-config --cflags)
CFLAGS ?= $(WARNFLAGS) $(OPTFLAGS) -std=c17
LDFLAGS ?= -flto=thin -Wl,-O2 -Wl,--gc-sections
LDLIBS = $(shell pcre2-config --libs8)

# FIXED: Added src/replace.c to the compilation list
SRCS = src/string_view.c src/match.c src/replace.c src/aggregate.c src/pipeline.c src/main.c
OBJS = $(SRCS:.c=.o)
TARGET = rx

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean
