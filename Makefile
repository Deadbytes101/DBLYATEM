CC ?= cc
CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
          -Wshadow -Wstrict-prototypes -Wmissing-prototypes

BUILD := build
CORE := src/pe.c src/map.c src/reloc.c src/reloc_apply.c src/import.c src/bind.c \
        src/iat_plan.c src/iat_apply.c
HEADERS := include/dblyat/pe.h include/dblyat/map.h include/dblyat/reloc.h \
           include/dblyat/reloc_apply.h include/dblyat/import.h \
           include/dblyat/bind.h include/dblyat/iat_plan.h \
           include/dblyat/iat_apply.h

.PHONY: all test clean

all: $(BUILD)/dblyatem

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/dblyatem: main.c $(CORE) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) main.c $(CORE) -o $@

$(BUILD)/pe_test: tests/pe_test.c src/pe.c include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/pe_test.c src/pe.c -o $@

$(BUILD)/map_test: tests/map_test.c src/pe.c src/map.c \
                   include/dblyat/pe.h include/dblyat/map.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/map_test.c src/pe.c src/map.c -o $@

$(BUILD)/reloc_test: tests/reloc_test.c src/pe.c src/map.c src/reloc.c \
                       include/dblyat/pe.h include/dblyat/map.h \
                       include/dblyat/reloc.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/reloc_test.c \
		src/pe.c src/map.c src/reloc.c -o $@

$(BUILD)/reloc_apply_test: tests/reloc_apply_test.c src/pe.c src/map.c \
                             src/reloc.c src/reloc_apply.c \
                             include/dblyat/pe.h include/dblyat/map.h \
                             include/dblyat/reloc.h \
                             include/dblyat/reloc_apply.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/reloc_apply_test.c \
		src/pe.c src/map.c src/reloc.c src/reloc_apply.c -o $@

$(BUILD)/import_test: tests/import_test.c src/pe.c src/map.c src/import.c \
                      include/dblyat/pe.h include/dblyat/map.h \
                      include/dblyat/import.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/import_test.c \
		src/pe.c src/map.c src/import.c -o $@

$(BUILD)/bind_test: tests/bind_test.c src/bind.c include/dblyat/bind.h \
                    include/dblyat/import.h include/dblyat/map.h \
                    include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/bind_test.c src/bind.c -o $@

$(BUILD)/iat_plan_test: tests/iat_plan_test.c src/iat_plan.c \
                        include/dblyat/iat_plan.h include/dblyat/bind.h \
                        include/dblyat/import.h include/dblyat/map.h \
                        include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/iat_plan_test.c src/iat_plan.c -o $@

$(BUILD)/iat_apply_test: tests/iat_apply_test.c src/iat_apply.c \
                         include/dblyat/iat_apply.h \
                         include/dblyat/iat_plan.h include/dblyat/bind.h \
                         include/dblyat/import.h include/dblyat/map.h \
                         include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/iat_apply_test.c src/iat_apply.c -o $@

$(BUILD)/pe_structs_test: tests/pe_structs_test.c include/dblyat/pe_structs.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/pe_structs_test.c -o $@

test: $(BUILD)/pe_test $(BUILD)/map_test $(BUILD)/reloc_test \
      $(BUILD)/reloc_apply_test $(BUILD)/import_test $(BUILD)/bind_test \
      $(BUILD)/iat_plan_test $(BUILD)/iat_apply_test \
      $(BUILD)/pe_structs_test
	./$(BUILD)/pe_test
	./$(BUILD)/map_test
	./$(BUILD)/reloc_test
	./$(BUILD)/reloc_apply_test
	./$(BUILD)/import_test
	./$(BUILD)/bind_test
	./$(BUILD)/iat_plan_test
	./$(BUILD)/iat_apply_test
	./$(BUILD)/pe_structs_test

clean:
	rm -rf $(BUILD)
