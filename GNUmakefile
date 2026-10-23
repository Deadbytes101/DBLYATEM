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

CORE += src/win32_translated_gateway_bridge.c \
        src/win32_translated_execution.c \
        src/translated_terminal_exit.c \
        src/linux_arm64_translation_entry.c
HEADERS += include/dblyat/win32_translated_gateway_bridge.h \
           include/dblyat/win32_translated_execution.h \
           include/dblyat/translated_terminal_exit.h \
           include/dblyat/linux_arm64_translation_entry.h \
           include/dblyat/arm64_published_block.h

HOST_MACHINE := $(shell uname -m)
ifneq ($(filter aarch64 arm64,$(HOST_MACHINE)),)
CORE += src/linux_arm64_translation_entry_aarch64.S
$(BUILD)/dblyatem: src/linux_arm64_translation_entry_aarch64.S
endif

$(BUILD)/dblyatem: src/win32_translated_gateway_bridge.c \
                   src/win32_translated_execution.c \
                   src/translated_terminal_exit.c \
                   src/linux_arm64_translation_entry.c \
                   include/dblyat/win32_translated_gateway_bridge.h \
                   include/dblyat/win32_translated_execution.h \
                   include/dblyat/translated_terminal_exit.h \
                   include/dblyat/linux_arm64_translation_entry.h \
                   include/dblyat/arm64_published_block.h

CORE += src/arm64_block_cache.c \
        src/win32_translated_run.c
HEADERS += include/dblyat/arm64_block_cache.h \
           include/dblyat/win32_translated_run.h

$(BUILD)/dblyatem: src/arm64_block_cache.c \
                   src/win32_translated_run.c \
                   src/win32_translated_run_internal.h \
                   include/dblyat/arm64_block_cache.h \
                   include/dblyat/win32_translated_run.h

CORE += src/arm64_translation_store.c \
        src/arm64_published_block.c \
        src/linux_arm64_wx_provider.c \
        src/x64_block.c \
        src/x64_decode.c \
        src/x64_memory_instruction.c \
        src/x64_memory_decode.c \
        src/ir.c \
        src/x64_ir_lower.c \
        src/x64_ir_lower_validate.c \
        src/x64_ir_lower_emit.c \
        src/arm64_select.c \
        src/arm64_select_validate.c \
        src/arm64_select_emit.c \
        src/x64_arm64_plan.c \
        src/x64_arm64_plan_validate.c \
        src/x64_arm64_plan_group.c \
        src/x64_arm64_plan_emit.c \
        src/x64_arm64_encode.c \
        src/x64_arm64_relocate.c \
        src/x64_arm64_relocate_validate.c \
        src/x64_arm64_relocate_emit.c \
        src/arm64_code_image.c \
        src/win32_translated_demand_run.c
HEADERS += include/dblyat/arm64_translation_store.h \
           include/dblyat/win32_translated_demand_run.h

$(BUILD)/dblyatem: src/arm64_translation_store.c \
                   src/arm64_published_block.c \
                   src/linux_arm64_wx_provider.c \
                   src/x64_block.c \
                   src/x64_decode.c \
                   src/x64_memory_instruction.c \
                   src/x64_memory_decode.c \
                   src/ir.c \
                   src/x64_ir_lower.c \
                   src/x64_ir_lower_validate.c \
                   src/x64_ir_lower_emit.c \
                   src/arm64_select.c \
                   src/arm64_select_validate.c \
                   src/arm64_select_emit.c \
                   src/x64_arm64_plan.c \
                   src/x64_arm64_plan_validate.c \
                   src/x64_arm64_plan_group.c \
                   src/x64_arm64_plan_emit.c \
                   src/x64_arm64_encode.c \
                   src/x64_arm64_relocate.c \
                   src/x64_arm64_relocate_validate.c \
                   src/x64_arm64_relocate_emit.c \
                   src/arm64_code_image.c \
                   src/win32_translated_demand_run.c \
                   src/win32_translated_demand_run_internal.h \
                   include/dblyat/arm64_translation_store.h \
                   include/dblyat/win32_translated_demand_run.h

CORE += src/win32_provider_tables.c \
        src/win32_pe_startup.c
HEADERS += include/dblyat/win32_provider_tables.h \
           include/dblyat/win32_pe_startup.h

$(BUILD)/dblyatem: src/win32_provider_tables.c \
                   src/win32_pe_startup.c \
                   include/dblyat/win32_provider_tables.h \
                   include/dblyat/win32_pe_startup.h

WIN32_PE_STARTUP_TEST_SOURCES := \
    src/pe.c src/map.c src/reloc.c src/reloc_apply.c \
    src/import.c src/bind.c src/iat_plan.c src/iat_apply.c \
    src/gateway_plan.c src/gateway_emit.c src/gateway_decode.c \
    src/win32_provider.c src/win32_provider_tables.c src/win32_pe_startup.c \
    src/x64_block.c src/x64_decode.c src/x64_memory_instruction.c \
    src/x64_memory_decode.c

$(BUILD)/win32_provider_tables_test: tests/win32_provider_tables_test.c \
                                      src/win32_provider.c \
                                      src/win32_provider_tables.c \
                                      include/dblyat/win32_provider_tables.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/win32_provider_tables_test.c \
		src/win32_provider.c src/win32_provider_tables.c -o $@

$(BUILD)/win32_pe_startup_test: tests/win32_pe_startup_test.c \
                                $(WIN32_PE_STARTUP_TEST_SOURCES) \
                                include/dblyat/win32_pe_startup.h \
                                include/dblyat/win32_provider_tables.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/win32_pe_startup_test.c \
		$(WIN32_PE_STARTUP_TEST_SOURCES) -o $@

.PHONY: win32_provider_tables_verify win32_pe_startup_verify
win32_provider_tables_verify: $(BUILD)/win32_provider_tables_test
	./$(BUILD)/win32_provider_tables_test

win32_pe_startup_verify: $(BUILD)/win32_pe_startup_test
	./$(BUILD)/win32_pe_startup_test

test: win32_provider_tables_verify win32_pe_startup_verify


# Issue 153: bounded host PE run entry and exact CLI contract checks.
CORE += src/host_pe_run.c src/linux_arm64_wx_posix.c
HEADERS += include/dblyat/host_pe_run.h
$(BUILD)/dblyatem: src/host_pe_run.c src/linux_arm64_wx_posix.c \
                   src/linux_arm64_translation_entry.c \
                   include/dblyat/host_pe_run.h
$(BUILD)/host_pe_run_plan_test: tests/host_pe_run_plan_test.c \
                                 src/host_pe_run.c \
                                 include/dblyat/host_pe_run.h | $(BUILD)
	$(CC) $(CPPFLAGS) -DDB_HOST_PE_RUN_PLAN_ONLY $(CFLAGS) tests/host_pe_run_plan_test.c \
		src/host_pe_run.c -o $@

.PHONY: host_pe_run_plan_verify
host_pe_run_plan_verify: $(BUILD)/host_pe_run_plan_test
	./$(BUILD)/host_pe_run_plan_test
test: host_pe_run_plan_verify
