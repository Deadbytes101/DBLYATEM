include Makefile

TEST_WRITE_CONTRACT := tests/test_write_contract.h
TEST_BINARIES := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/*_test.c))

$(TEST_BINARIES): $(TEST_WRITE_CONTRACT)
$(TEST_BINARIES): CPPFLAGS += -include $(TEST_WRITE_CONTRACT)

CORE += src/gateway_roundtrip.c
HEADERS += include/dblyat/gateway_roundtrip.h

$(BUILD)/dblyatem: src/gateway_roundtrip.c include/dblyat/gateway_roundtrip.h

$(BUILD)/gateway_roundtrip_test: tests/gateway_roundtrip_test.c \
                                 src/gateway_roundtrip.c \
                                 src/gateway_enter.c \
                                 src/gateway_lookup.c \
                                 src/gateway_dispatch.c \
                                 src/gateway_frame.c \
                                 src/gateway_complete.c \
                                 src/gateway_return.c \
                                 src/gateway_return_apply.c \
                                 include/dblyat/gateway_roundtrip.h \
                                 include/dblyat/gateway_enter.h \
                                 include/dblyat/gateway_complete.h \
                                 include/dblyat/gateway_return_apply.h \
                                 include/dblyat/gateway_return.h \
                                 include/dblyat/gateway_frame.h \
                                 include/dblyat/gateway_dispatch.h \
                                 include/dblyat/gateway_lookup.h \
                                 include/dblyat/gateway_decode.h \
                                 include/dblyat/gateway_emit.h \
                                 include/dblyat/gateway_plan.h \
                                 include/dblyat/iat_plan.h \
                                 include/dblyat/bind.h \
                                 include/dblyat/import.h \
                                 include/dblyat/map.h \
                                 include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gateway_roundtrip_test.c \
		src/gateway_roundtrip.c src/gateway_enter.c \
		src/gateway_lookup.c src/gateway_dispatch.c \
		src/gateway_frame.c src/gateway_complete.c \
		src/gateway_return.c src/gateway_return_apply.c -o $@

.PHONY: gateway_roundtrip_verify
gateway_roundtrip_verify: $(BUILD)/gateway_roundtrip_test
	./$(BUILD)/gateway_roundtrip_test

test: gateway_roundtrip_verify

CORE += src/gateway_invoke.c
HEADERS += include/dblyat/gateway_invoke.h

$(BUILD)/dblyatem: src/gateway_invoke.c include/dblyat/gateway_invoke.h

$(BUILD)/gateway_invoke_test: tests/gateway_invoke_test.c \
                              src/gateway_invoke.c \
                              include/dblyat/gateway_invoke.h \
                              include/dblyat/gateway_enter.h \
                              include/dblyat/gateway_return_apply.h \
                              include/dblyat/gateway_return.h \
                              include/dblyat/gateway_frame.h \
                              include/dblyat/gateway_dispatch.h \
                              include/dblyat/gateway_lookup.h \
                              include/dblyat/gateway_decode.h \
                              include/dblyat/gateway_emit.h \
                              include/dblyat/gateway_plan.h \
                              include/dblyat/iat_plan.h \
                              include/dblyat/bind.h \
                              include/dblyat/import.h \
                              include/dblyat/map.h \
                              include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gateway_invoke_test.c \
		src/gateway_invoke.c -o $@

.PHONY: gateway_invoke_verify
gateway_invoke_verify: $(BUILD)/gateway_invoke_test
	./$(BUILD)/gateway_invoke_test

test: gateway_invoke_verify

CORE += src/gateway_result.c
HEADERS += include/dblyat/gateway_result.h

$(BUILD)/dblyatem: src/gateway_result.c include/dblyat/gateway_result.h

$(BUILD)/gateway_result_test: tests/gateway_result_test.c \
                              src/gateway_result.c \
                              include/dblyat/gateway_result.h \
                              include/dblyat/gateway_invoke.h \
                              include/dblyat/gateway_enter.h \
                              include/dblyat/gateway_return_apply.h \
                              include/dblyat/gateway_return.h \
                              include/dblyat/gateway_frame.h \
                              include/dblyat/gateway_dispatch.h \
                              include/dblyat/gateway_lookup.h \
                              include/dblyat/gateway_decode.h \
                              include/dblyat/gateway_emit.h \
                              include/dblyat/gateway_plan.h \
                              include/dblyat/iat_plan.h \
                              include/dblyat/bind.h \
                              include/dblyat/import.h \
                              include/dblyat/map.h \
                              include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gateway_result_test.c \
		src/gateway_result.c -o $@

.PHONY: gateway_result_verify
gateway_result_verify: $(BUILD)/gateway_result_test
	./$(BUILD)/gateway_result_test

test: gateway_result_verify

CORE += src/gateway_auth_complete.c
HEADERS += include/dblyat/gateway_auth_complete.h

$(BUILD)/dblyatem: src/gateway_auth_complete.c \
                   include/dblyat/gateway_auth_complete.h

$(BUILD)/gateway_auth_complete_test: tests/gateway_auth_complete_test.c \
                                     src/gateway_auth_complete.c \
                                     src/gateway_result.c \
                                     include/dblyat/gateway_auth_complete.h \
                                     include/dblyat/gateway_result.h \
                                     include/dblyat/gateway_invoke.h \
                                     include/dblyat/gateway_complete.h \
                                     include/dblyat/gateway_return_apply.h \
                                     include/dblyat/gateway_return.h \
                                     include/dblyat/gateway_frame.h \
                                     include/dblyat/gateway_enter.h \
                                     include/dblyat/gateway_dispatch.h \
                                     include/dblyat/gateway_lookup.h \
                                     include/dblyat/gateway_decode.h \
                                     include/dblyat/gateway_emit.h \
                                     include/dblyat/gateway_plan.h \
                                     include/dblyat/iat_plan.h \
                                     include/dblyat/bind.h \
                                     include/dblyat/import.h \
                                     include/dblyat/map.h \
                                     include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gateway_auth_complete_test.c \
		src/gateway_auth_complete.c src/gateway_result.c -o $@

.PHONY: gateway_auth_complete_verify
gateway_auth_complete_verify: $(BUILD)/gateway_auth_complete_test
	./$(BUILD)/gateway_auth_complete_test

test: gateway_auth_complete_verify

CORE += src/gateway_executor.c
HEADERS += include/dblyat/gateway_executor.h

$(BUILD)/dblyatem: src/gateway_executor.c \
                   include/dblyat/gateway_executor.h

$(BUILD)/gateway_executor_test: tests/gateway_executor_test.c \
                                src/gateway_executor.c \
                                src/gateway_auth_complete.c \
                                src/gateway_result.c \
                                include/dblyat/gateway_executor.h \
                                include/dblyat/gateway_auth_complete.h \
                                include/dblyat/gateway_result.h \
                                include/dblyat/gateway_invoke.h \
                                include/dblyat/gateway_complete.h \
                                include/dblyat/gateway_return_apply.h \
                                include/dblyat/gateway_return.h \
                                include/dblyat/gateway_frame.h \
                                include/dblyat/gateway_enter.h \
                                include/dblyat/gateway_dispatch.h \
                                include/dblyat/gateway_lookup.h \
                                include/dblyat/gateway_decode.h \
                                include/dblyat/gateway_emit.h \
                                include/dblyat/gateway_plan.h \
                                include/dblyat/iat_plan.h \
                                include/dblyat/bind.h \
                                include/dblyat/import.h \
                                include/dblyat/map.h \
                                include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gateway_executor_test.c \
		src/gateway_executor.c src/gateway_auth_complete.c \
		src/gateway_result.c -o $@

.PHONY: gateway_executor_verify
gateway_executor_verify: $(BUILD)/gateway_executor_test
	./$(BUILD)/gateway_executor_test

test: gateway_executor_verify

CORE += src/gateway_registry.c
HEADERS += include/dblyat/gateway_registry.h

$(BUILD)/dblyatem: src/gateway_registry.c \
                   include/dblyat/gateway_registry.h

$(BUILD)/gateway_registry_test: tests/gateway_registry_test.c \
                                src/gateway_registry.c \
                                src/gateway_executor.c \
                                src/gateway_auth_complete.c \
                                src/gateway_result.c \
                                include/dblyat/gateway_registry.h \
                                include/dblyat/gateway_executor.h \
                                include/dblyat/gateway_auth_complete.h \
                                include/dblyat/gateway_result.h \
                                include/dblyat/gateway_invoke.h \
                                include/dblyat/gateway_complete.h \
                                include/dblyat/gateway_return_apply.h \
                                include/dblyat/gateway_return.h \
                                include/dblyat/gateway_frame.h \
                                include/dblyat/gateway_enter.h \
                                include/dblyat/gateway_dispatch.h \
                                include/dblyat/gateway_lookup.h \
                                include/dblyat/gateway_decode.h \
                                include/dblyat/gateway_emit.h \
                                include/dblyat/gateway_plan.h \
                                include/dblyat/iat_plan.h \
                                include/dblyat/bind.h \
                                include/dblyat/import.h \
                                include/dblyat/map.h \
                                include/dblyat/pe.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gateway_registry_test.c \
		src/gateway_registry.c src/gateway_executor.c \
		src/gateway_auth_complete.c src/gateway_result.c -o $@

.PHONY: gateway_registry_verify
gateway_registry_verify: $(BUILD)/gateway_registry_test
	./$(BUILD)/gateway_registry_test

test: gateway_registry_verify

CORE += src/win32_provider.c \
        src/win32_kernel32.c \
        src/win32_kernel32_invoke.c \
        src/win32_kernel32_gateway.c \
        src/win32_runtime_dispatch.c
HEADERS += include/dblyat/win32_provider.h \
           include/dblyat/win32_kernel32.h \
           include/dblyat/win32_kernel32_invoke.h \
           include/dblyat/win32_kernel32_gateway.h \
           include/dblyat/win32_runtime_dispatch.h

$(BUILD)/dblyatem: src/win32_provider.c \
                   src/win32_kernel32.c \
                   src/win32_kernel32_invoke.c \
                   src/win32_kernel32_gateway.c \
                   src/win32_runtime_dispatch.c \
                   include/dblyat/win32_provider.h \
                   include/dblyat/win32_kernel32.h \
                   include/dblyat/win32_kernel32_invoke.h \
                   include/dblyat/win32_kernel32_gateway.h \
                   include/dblyat/win32_runtime_dispatch.h
