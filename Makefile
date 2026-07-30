CC ?= cc
CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
          -Wshadow -Wstrict-prototypes -Wmissing-prototypes

BUILD := build
CORE := src/pe.c src/map.c
HEADERS := include/dblyat/pe.h include/dblyat/map.h

.PHONY: all test clean

all: $(BUILD)/dblyatem

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/dblyatem: main.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(CORE) -o $@

$(BUILD)/pe_test: tests/pe_test.c src/pe.c include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/pe_test.c src/pe.c -o $@

$(BUILD)/map_test: tests/map_test.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/map_test.c $(CORE) -o $@

test: $(BUILD)/pe_test $(BUILD)/map_test
	./$(BUILD)/pe_test
	./$(BUILD)/map_test

clean:
	rm -rf $(BUILD)
