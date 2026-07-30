CC ?= cc
CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
          -Wshadow -Wstrict-prototypes -Wmissing-prototypes

BUILD := build
CORE := src/pe.c

.PHONY: all test clean

all: $(BUILD)/dblyatem

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/dblyatem: main.c $(CORE) include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(CORE) -o $@

$(BUILD)/pe_test: tests/pe_test.c $(CORE) include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/pe_test.c $(CORE) -o $@

test: $(BUILD)/pe_test
	./$(BUILD)/pe_test

clean:
	rm -rf $(BUILD)
