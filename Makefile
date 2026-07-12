CC = clang
CFLAGS = -Wall -Wextra -Wpedantic -O3 -std=c17 -Iinclude $(shell pcre2-config --cflags)
LIBS = $(shell pcre2-config --libs8)

# FIXED: Added src/replace.c to the compilation list
SRCS = src/string_view.c src/match.c src/replace.c src/aggregate.c src/pipeline.c src/main.c
OBJS = $(SRCS:.c=.o)
TARGET = rx

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o $(TARGET)

.PHONY: all clean
