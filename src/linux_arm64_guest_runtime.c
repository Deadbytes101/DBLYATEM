#include "linux_arm64_guest_runtime_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DB_GUEST_ARITH_FLAGS \
    (DB_X64_RFLAGS_CF | DB_X64_RFLAGS_PF | DB_X64_RFLAGS_AF | \
     DB_X64_RFLAGS_ZF | DB_X64_RFLAGS_SF | DB_X64_RFLAGS_OF)
#define DB_GUEST_RSP_INDEX UINT8_C(4)

#if defined(__aarch64__)
extern void db_linux_arm64_helper_form_guest_address(void);
extern void db_linux_arm64_helper_read_guest_u64(void);
extern void db_linux_arm64_helper_write_guest_u64(void);
extern void db_linux_arm64_helper_push_guest_u64(void);
extern void db_linux_arm64_helper_pop_guest_u64(void);
extern void db_linux_arm64_helper_update_logic_flags(void);
extern void db_linux_arm64_helper_test_guest_condition(void);
#endif

static bool zero_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) return false;
    }
    return true;
}

static bool overlap_address(uint64_t left,
                            uint64_t left_size,
                            uint64_t right,
                            uint64_t right_size)
{
    if (left_size == 0u || right_size == 0u) return false;
    return left <= right ? right - left < left_size
                         : left - right < right_size;
}

static bool pointer_range_valid(uint64_t address, uint64_t size)
{
    if (address == 0u || size == 0u ||
        address > (uint64_t)UINTPTR_MAX ||
        size - UINT64_C(1) > (uint64_t)UINTPTR_MAX - address) {
        return false;
    }
    return true;
}

static bool canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return address != 0u &&
           (sign == 0u ? upper == 0u : upper == UINT64_C(0xffff));
}

static bool guest_range_valid(uint64_t base,
                              uint64_t size,
                              uint64_t *last)
{
    uint64_t end;

    if (size == 0u || !canonical(base) ||
        size - UINT64_C(1) > UINT64_MAX - base) {
        return false;
    }
    end = base + size - UINT64_C(1);
    if (!canonical(end)) return false;
    if (last != NULL) *last = end;
    return true;
}

static bool valid_protection(db_map_page_protection protection)
{
    return protection == DB_MAP_PAGE_NONE ||
           protection == DB_MAP_PAGE_R ||
           protection == DB_MAP_PAGE_RW ||
           protection == DB_MAP_PAGE_RX;
}

static bool page_allows(db_map_page_protection protection,
                        db_linux_arm64_guest_access access)
{
    if (access == DB_LINUX_ARM64_GUEST_ACCESS_READ) {
        return (protection & DB_MAP_PAGE_R) != 0u;
    }
    if (access == DB_LINUX_ARM64_GUEST_ACCESS_WRITE) {
        return protection == DB_MAP_PAGE_RW;
    }
    return false;
}

static bool memory_descriptor_valid(
    const db_linux_arm64_guest_memory *memory)
{
    const db_map_page_protection *page_plan;
    uint64_t expected_page_count;
    uint64_t image_last;
    uint64_t stack_last;
    uint64_t gateway_last = 0u;
    uint64_t index;

    if (memory == NULL ||
        memory->format_version !=
            DB_LINUX_ARM64_GUEST_MEMORY_FORMAT_VERSION ||
        !zero_bytes(memory->reserved, sizeof(memory->reserved)) ||
        memory->image_page_size < UINT64_C(4096) ||
        (memory->image_page_size & (memory->image_page_size - UINT64_C(1))) !=
            0u ||
        !pointer_range_valid(memory->image_host_address,
                             memory->image_size) ||
        !pointer_range_valid(memory->stack_host_address,
                             memory->stack_size) ||
        memory->image_page_plan_address == 0u ||
        memory->image_page_plan_address > (uint64_t)UINTPTR_MAX ||
        !guest_range_valid(memory->image_guest_base,
                           memory->image_size,
                           &image_last) ||
        !guest_range_valid(memory->stack_guest_base,
                           memory->stack_size,
                           &stack_last)) {
        return false;
    }

    expected_page_count = memory->image_size / memory->image_page_size;
    if ((memory->image_size % memory->image_page_size) != 0u) {
        if (expected_page_count == UINT64_MAX) return false;
        ++expected_page_count;
    }
    if (memory->image_page_count != expected_page_count ||
        memory->image_page_count == 0u ||
        memory->image_page_count > (uint64_t)SIZE_MAX) {
        return false;
    }

    page_plan = (const db_map_page_protection *)(uintptr_t)
        memory->image_page_plan_address;
    for (index = 0u; index < memory->image_page_count; ++index) {
        if (!valid_protection(page_plan[index])) return false;
    }

    if (overlap_address(memory->image_guest_base,
                        memory->image_size,
                        memory->stack_guest_base,
                        memory->stack_size) ||
        overlap_address(memory->image_host_address,
                        memory->image_size,
                        memory->stack_host_address,
                        memory->stack_size)) {
        return false;
    }

    if (memory->gateway_size == 0u) {
        if (memory->gateway_host_address != 0u ||
            memory->gateway_guest_base != 0u) {
            return false;
        }
        return true;
    }

    if (!pointer_range_valid(memory->gateway_host_address,
                             memory->gateway_size) ||
        !guest_range_valid(memory->gateway_guest_base,
                           memory->gateway_size,
                           &gateway_last) ||
        overlap_address(memory->image_guest_base,
                        memory->image_size,
                        memory->gateway_guest_base,
                        memory->gateway_size) ||
        overlap_address(memory->stack_guest_base,
                        memory->stack_size,
                        memory->gateway_guest_base,
                        memory->gateway_size) ||
        overlap_address(memory->image_host_address,
                        memory->image_size,
                        memory->gateway_host_address,
                        memory->gateway_size) ||
        overlap_address(memory->stack_host_address,
                        memory->stack_size,
                        memory->gateway_host_address,
                        memory->gateway_size)) {
        return false;
    }

    (void)image_last;
    (void)stack_last;
    (void)gateway_last;
    return true;
}

static bool output_overlaps_inputs(
    const db_linux_arm64_guest_memory *output,
    const db_linux_arm64_guest_memory *memory)
{
    const uint64_t out = (uint64_t)(uintptr_t)output;
    const uint64_t out_size = (uint64_t)sizeof(*output);

    return overlap_address(out, out_size,
                           memory->image_host_address,
                           memory->image_size) ||
           overlap_address(out, out_size,
                           memory->stack_host_address,
                           memory->stack_size) ||
           (memory->gateway_size != 0u &&
            overlap_address(out, out_size,
                            memory->gateway_host_address,
                            memory->gateway_size)) ||
           overlap_address(out, out_size,
                           memory->image_page_plan_address,
                           memory->image_page_count);
}

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_init(
    db_linux_arm64_guest_memory *memory,
    void *image,
    size_t image_size,
    const db_map_page_protection *image_page_plan,
    size_t image_page_count,
    size_t image_page_size,
    uint64_t image_guest_base,
    void *stack,
    size_t stack_size,
    uint64_t stack_guest_base,
    const void *gateway,
    size_t gateway_size,
    uint64_t gateway_guest_base)
{
    db_linux_arm64_guest_memory completed;

    if (memory == NULL || image == NULL || image_page_plan == NULL ||
        stack == NULL || (gateway_size != 0u && gateway == NULL)) {
        return DB_LINUX_ARM64_GUEST_RUNTIME_NULL_ARGUMENT;
    }

    (void)memset(&completed, 0, sizeof(completed));
    completed.image_host_address = (uint64_t)(uintptr_t)image;
    completed.image_guest_base = image_guest_base;
    completed.image_size = (uint64_t)image_size;
    completed.image_page_plan_address =
        (uint64_t)(uintptr_t)image_page_plan;
    completed.image_page_count = (uint64_t)image_page_count;
    completed.image_page_size = (uint64_t)image_page_size;
    completed.stack_host_address = (uint64_t)(uintptr_t)stack;
    completed.stack_guest_base = stack_guest_base;
    completed.stack_size = (uint64_t)stack_size;
    if (gateway_size != 0u) {
        completed.gateway_host_address = (uint64_t)(uintptr_t)gateway;
        completed.gateway_guest_base = gateway_guest_base;
        completed.gateway_size = (uint64_t)gateway_size;
    }
    completed.format_version = DB_LINUX_ARM64_GUEST_MEMORY_FORMAT_VERSION;

    if (!memory_descriptor_valid(&completed) ||
        output_overlaps_inputs(memory, &completed)) {
        return DB_LINUX_ARM64_GUEST_RUNTIME_BAD_MEMORY;
    }

    *memory = completed;
    return DB_LINUX_ARM64_GUEST_RUNTIME_OK;
}

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_validate(
    const db_linux_arm64_guest_memory *memory)
{
    if (memory == NULL) return DB_LINUX_ARM64_GUEST_RUNTIME_NULL_ARGUMENT;
    return memory_descriptor_valid(memory)
               ? DB_LINUX_ARM64_GUEST_RUNTIME_OK
               : DB_LINUX_ARM64_GUEST_RUNTIME_BAD_MEMORY;
}

static db_linux_arm64_guest_runtime_status resolve(
    const db_linux_arm64_guest_memory *memory,
    uint64_t guest_address,
    uint64_t byte_count,
    db_linux_arm64_guest_access access,
    uint8_t **host)
{
    uint64_t guest_last;
    uint64_t offset;

    if (!memory_descriptor_valid(memory)) {
        return DB_LINUX_ARM64_GUEST_RUNTIME_BAD_MEMORY;
    }
    if (!canonical(guest_address)) {
        return DB_LINUX_ARM64_GUEST_RUNTIME_NONCANONICAL_ADDRESS;
    }
    if (byte_count == 0u || byte_count - UINT64_C(1) >
                               UINT64_MAX - guest_address) {
        return DB_LINUX_ARM64_GUEST_RUNTIME_ADDRESS_OVERFLOW;
    }
    guest_last = guest_address + byte_count - UINT64_C(1);
    if (!canonical(guest_last)) {
        return DB_LINUX_ARM64_GUEST_RUNTIME_NONCANONICAL_ADDRESS;
    }

    if (guest_address >= memory->image_guest_base &&
        guest_last - memory->image_guest_base < memory->image_size &&
        guest_last >= guest_address) {
        const db_map_page_protection *page_plan =
            (const db_map_page_protection *)(uintptr_t)
                memory->image_page_plan_address;
        uint64_t first_page;
        uint64_t last_page;
        uint64_t page;

        offset = guest_address - memory->image_guest_base;
        first_page = offset / memory->image_page_size;
        last_page = (offset + byte_count - UINT64_C(1)) /
                    memory->image_page_size;
        if (last_page >= memory->image_page_count) {
            return DB_LINUX_ARM64_GUEST_RUNTIME_NOT_MAPPED;
        }
        for (page = first_page; page <= last_page; ++page) {
            if (!page_allows(page_plan[page], access)) {
                return DB_LINUX_ARM64_GUEST_RUNTIME_ACCESS_DENIED;
            }
        }
        *host = (uint8_t *)(uintptr_t)(memory->image_host_address + offset);
        return DB_LINUX_ARM64_GUEST_RUNTIME_OK;
    }

    if (guest_address >= memory->stack_guest_base &&
        guest_last - memory->stack_guest_base < memory->stack_size &&
        guest_last >= guest_address) {
        offset = guest_address - memory->stack_guest_base;
        *host = (uint8_t *)(uintptr_t)(memory->stack_host_address + offset);
        return access == DB_LINUX_ARM64_GUEST_ACCESS_READ ||
                       access == DB_LINUX_ARM64_GUEST_ACCESS_WRITE
                   ? DB_LINUX_ARM64_GUEST_RUNTIME_OK
                   : DB_LINUX_ARM64_GUEST_RUNTIME_ACCESS_DENIED;
    }

    if (memory->gateway_size != 0u &&
        guest_address >= memory->gateway_guest_base &&
        guest_last - memory->gateway_guest_base < memory->gateway_size &&
        guest_last >= guest_address) {
        if (access != DB_LINUX_ARM64_GUEST_ACCESS_READ) {
            return DB_LINUX_ARM64_GUEST_RUNTIME_ACCESS_DENIED;
        }
        offset = guest_address - memory->gateway_guest_base;
        *host = (uint8_t *)(uintptr_t)(memory->gateway_host_address + offset);
        return DB_LINUX_ARM64_GUEST_RUNTIME_OK;
    }

    return DB_LINUX_ARM64_GUEST_RUNTIME_NOT_MAPPED;
}

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_read_u64(
    const db_linux_arm64_guest_memory *memory,
    uint64_t guest_address,
    uint64_t *value)
{
    uint8_t *host = NULL;
    uint64_t completed = 0u;
    uint32_t index;
    db_linux_arm64_guest_runtime_status status;

    if (memory == NULL || value == NULL) {
        return DB_LINUX_ARM64_GUEST_RUNTIME_NULL_ARGUMENT;
    }
    status = resolve(memory,
                     guest_address,
                     UINT64_C(8),
                     DB_LINUX_ARM64_GUEST_ACCESS_READ,
                     &host);
    if (status != DB_LINUX_ARM64_GUEST_RUNTIME_OK) return status;

    for (index = 0u; index < 8u; ++index) {
        completed |= (uint64_t)host[index] << (index * 8u);
    }
    *value = completed;
    return DB_LINUX_ARM64_GUEST_RUNTIME_OK;
}

db_linux_arm64_guest_runtime_status db_linux_arm64_guest_memory_write_u64(
    const db_linux_arm64_guest_memory *memory,
    uint64_t guest_address,
    uint64_t value)
{
    uint8_t *host = NULL;
    uint32_t index;
    db_linux_arm64_guest_runtime_status status;

    if (memory == NULL) return DB_LINUX_ARM64_GUEST_RUNTIME_NULL_ARGUMENT;
    status = resolve(memory,
                     guest_address,
                     UINT64_C(8),
                     DB_LINUX_ARM64_GUEST_ACCESS_WRITE,
                     &host);
    if (status != DB_LINUX_ARM64_GUEST_RUNTIME_OK) return status;

    for (index = 0u; index < 8u; ++index) {
        host[index] = (uint8_t)(value >> (index * 8u));
    }
    return DB_LINUX_ARM64_GUEST_RUNTIME_OK;
}

static bool context_faulted(
    const db_linux_arm64_translation_context *context)
{
    return context == NULL ||
           context->helper_fault !=
               (uint32_t)DB_LINUX_ARM64_HELPER_FAULT_NONE;
}

static void set_fault(db_linux_arm64_translation_context *context,
                      db_linux_arm64_helper_fault fault,
                      uint64_t address,
                      db_linux_arm64_guest_access access)
{
    if (context == NULL ||
        context->helper_fault !=
            (uint32_t)DB_LINUX_ARM64_HELPER_FAULT_NONE) {
        return;
    }
    context->helper_fault = (uint32_t)fault;
    context->helper_fault_address = address;
    context->helper_access = (uint32_t)access;
}

static bool guest_memory_bound(
    db_linux_arm64_translation_context *context)
{
    if (context->guest_memory_bound != 1u ||
        db_linux_arm64_guest_memory_validate(&context->guest_memory) !=
            DB_LINUX_ARM64_GUEST_RUNTIME_OK) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_UNBOUND_MEMORY,
                  0u,
                  DB_LINUX_ARM64_GUEST_ACCESS_NONE);
        return false;
    }
    return true;
}

static bool add_unsigned(uint64_t left, uint64_t right, uint64_t *out)
{
    if (UINT64_MAX - left < right) return false;
    *out = left + right;
    return true;
}

static bool add_signed(uint64_t base, int64_t displacement, uint64_t *out)
{
    uint64_t magnitude;

    if (displacement >= 0) {
        return add_unsigned(base, (uint64_t)displacement, out);
    }
    magnitude = (uint64_t)(-(displacement + INT64_C(1))) + UINT64_C(1);
    if (base < magnitude) return false;
    *out = base - magnitude;
    return true;
}

uint64_t db_linux_arm64_helper_form_guest_address_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t value,
    uint64_t displacement_bits,
    uint64_t address_mode,
    uint64_t base_guest_register,
    uint64_t index_guest_register,
    uint64_t scale)
{
    uint64_t address = 0u;
    uint64_t index_value = 0u;
    const int64_t displacement = (int64_t)displacement_bits;

    if (context_faulted(context)) return 0u;
    if (state == NULL || scale > UINT8_MAX ||
        (scale != 1u && scale != 2u && scale != 4u && scale != 8u) ||
        (base_guest_register != DB_IR_REGISTER_NONE &&
         base_guest_register >= 16u) ||
        (index_guest_register != DB_IR_REGISTER_NONE &&
         index_guest_register >= 16u)) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND,
                  0u,
                  DB_LINUX_ARM64_GUEST_ACCESS_NONE);
        return 0u;
    }

    switch ((db_ir_address_mode)address_mode) {
    case DB_IR_ADDRESS_BASE_INDEX:
        if (value != 0u ||
            (base_guest_register == DB_IR_REGISTER_NONE &&
             index_guest_register == DB_IR_REGISTER_NONE)) {
            set_fault(context,
                      DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND,
                      0u,
                      DB_LINUX_ARM64_GUEST_ACCESS_NONE);
            return 0u;
        }
        if (base_guest_register != DB_IR_REGISTER_NONE) {
            address = state->registers[base_guest_register];
        }
        if (index_guest_register != DB_IR_REGISTER_NONE) {
            const uint64_t raw = state->registers[index_guest_register];
            if (raw != 0u && scale > UINT64_MAX / raw) {
                set_fault(context,
                          DB_LINUX_ARM64_HELPER_FAULT_ADDRESS,
                          0u,
                          DB_LINUX_ARM64_GUEST_ACCESS_NONE);
                return 0u;
            }
            index_value = raw * scale;
            if (!add_unsigned(address, index_value, &address)) {
                set_fault(context,
                          DB_LINUX_ARM64_HELPER_FAULT_ADDRESS,
                          0u,
                          DB_LINUX_ARM64_GUEST_ACCESS_NONE);
                return 0u;
            }
        }
        if (!add_signed(address, displacement, &address)) {
            set_fault(context,
                      DB_LINUX_ARM64_HELPER_FAULT_ADDRESS,
                      0u,
                      DB_LINUX_ARM64_GUEST_ACCESS_NONE);
            return 0u;
        }
        break;

    case DB_IR_ADDRESS_IP_RELATIVE:
        if (!canonical(value) ||
            base_guest_register != DB_IR_REGISTER_NONE ||
            index_guest_register != DB_IR_REGISTER_NONE || scale != 1u ||
            !add_signed(value, displacement, &address)) {
            set_fault(context,
                      DB_LINUX_ARM64_HELPER_FAULT_ADDRESS,
                      value,
                      DB_LINUX_ARM64_GUEST_ACCESS_NONE);
            return 0u;
        }
        break;

    case DB_IR_ADDRESS_ABSOLUTE:
        if (value != 0u ||
            base_guest_register != DB_IR_REGISTER_NONE ||
            index_guest_register != DB_IR_REGISTER_NONE || scale != 1u) {
            set_fault(context,
                      DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND,
                      0u,
                      DB_LINUX_ARM64_GUEST_ACCESS_NONE);
            return 0u;
        }
        address = displacement_bits;
        break;

    case DB_IR_ADDRESS_NONE:
    default:
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND,
                  0u,
                  DB_LINUX_ARM64_GUEST_ACCESS_NONE);
        return 0u;
    }

    if (!canonical(address)) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_ADDRESS,
                  address,
                  DB_LINUX_ARM64_GUEST_ACCESS_NONE);
        return 0u;
    }
    return address;
}

uint64_t db_linux_arm64_helper_read_guest_u64_core(
    db_linux_arm64_translation_context *context,
    uint64_t guest_address)
{
    uint64_t value = 0u;
    db_linux_arm64_guest_runtime_status status;

    if (context_faulted(context)) return 0u;
    if (!guest_memory_bound(context)) return 0u;
    status = db_linux_arm64_guest_memory_read_u64(
        &context->guest_memory, guest_address, &value);
    if (status != DB_LINUX_ARM64_GUEST_RUNTIME_OK) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_READ,
                  guest_address,
                  DB_LINUX_ARM64_GUEST_ACCESS_READ);
        return 0u;
    }
    return value;
}

void db_linux_arm64_helper_write_guest_u64_core(
    db_linux_arm64_translation_context *context,
    uint64_t guest_address,
    uint64_t value)
{
    db_linux_arm64_guest_runtime_status status;

    if (context_faulted(context)) return;
    if (!guest_memory_bound(context)) return;
    status = db_linux_arm64_guest_memory_write_u64(
        &context->guest_memory, guest_address, value);
    if (status != DB_LINUX_ARM64_GUEST_RUNTIME_OK) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_WRITE,
                  guest_address,
                  DB_LINUX_ARM64_GUEST_ACCESS_WRITE);
    }
}

void db_linux_arm64_helper_push_guest_u64_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t value)
{
    uint64_t next_rsp;
    db_linux_arm64_guest_runtime_status status;

    if (context_faulted(context)) return;
    if (state == NULL || state->registers[DB_GUEST_RSP_INDEX] < UINT64_C(8)) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_STACK,
                  state == NULL ? 0u : state->registers[DB_GUEST_RSP_INDEX],
                  DB_LINUX_ARM64_GUEST_ACCESS_WRITE);
        return;
    }
    if (!guest_memory_bound(context)) return;
    next_rsp = state->registers[DB_GUEST_RSP_INDEX] - UINT64_C(8);
    if (!canonical(next_rsp)) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_STACK,
                  next_rsp,
                  DB_LINUX_ARM64_GUEST_ACCESS_WRITE);
        return;
    }
    status = db_linux_arm64_guest_memory_write_u64(
        &context->guest_memory, next_rsp, value);
    if (status != DB_LINUX_ARM64_GUEST_RUNTIME_OK) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_STACK,
                  next_rsp,
                  DB_LINUX_ARM64_GUEST_ACCESS_WRITE);
        return;
    }
    state->registers[DB_GUEST_RSP_INDEX] = next_rsp;
}

uint64_t db_linux_arm64_helper_pop_guest_u64_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context)
{
    uint64_t rsp;
    uint64_t next_rsp;
    uint64_t value = 0u;
    db_linux_arm64_guest_runtime_status status;

    if (context_faulted(context)) return 0u;
    if (state == NULL || !guest_memory_bound(context)) return 0u;
    rsp = state->registers[DB_GUEST_RSP_INDEX];
    if (!canonical(rsp) || UINT64_MAX - rsp < UINT64_C(8)) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_STACK,
                  rsp,
                  DB_LINUX_ARM64_GUEST_ACCESS_READ);
        return 0u;
    }
    next_rsp = rsp + UINT64_C(8);
    if (!canonical(next_rsp)) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_STACK,
                  rsp,
                  DB_LINUX_ARM64_GUEST_ACCESS_READ);
        return 0u;
    }
    status = db_linux_arm64_guest_memory_read_u64(
        &context->guest_memory, rsp, &value);
    if (status != DB_LINUX_ARM64_GUEST_RUNTIME_OK) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_STACK,
                  rsp,
                  DB_LINUX_ARM64_GUEST_ACCESS_READ);
        return 0u;
    }
    state->registers[DB_GUEST_RSP_INDEX] = next_rsp;
    return value;
}

static bool parity_even(uint8_t value)
{
    value ^= (uint8_t)(value >> 4u);
    value &= UINT8_C(0x0f);
    return ((UINT16_C(0x6996) >> value) & UINT16_C(1)) == 0u;
}

void db_linux_arm64_helper_update_logic_flags_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t width,
    uint64_t guest_register)
{
    uint64_t result;
    uint64_t flags;
    uint64_t sign;

    if (context_faulted(context)) return;
    if (state == NULL || guest_register >= 16u ||
        (width != (uint64_t)DB_IR_WIDTH_32 &&
         width != (uint64_t)DB_IR_WIDTH_64)) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND,
                  0u,
                  DB_LINUX_ARM64_GUEST_ACCESS_NONE);
        return;
    }

    result = state->registers[guest_register];
    if (width == (uint64_t)DB_IR_WIDTH_32) {
        result = (uint64_t)(uint32_t)result;
        sign = UINT64_C(0x80000000);
    } else {
        sign = UINT64_C(0x8000000000000000);
    }
    flags = state->rflags & ~DB_GUEST_ARITH_FLAGS;
    if (result == 0u) flags |= DB_X64_RFLAGS_ZF;
    if ((result & sign) != 0u) flags |= DB_X64_RFLAGS_SF;
    if (parity_even((uint8_t)result)) flags |= DB_X64_RFLAGS_PF;
    state->rflags = flags | DB_X64_RFLAGS_FIXED_ONE;
}

uint64_t db_linux_arm64_helper_test_guest_condition_core(
    db_x64_scalar_state *state,
    db_linux_arm64_translation_context *context,
    uint64_t condition)
{
    bool result;
    bool cf;
    bool pf;
    bool zf;
    bool sf;
    bool of;

    if (context_faulted(context)) return 0u;
    if (state == NULL || condition >= 16u) {
        set_fault(context,
                  DB_LINUX_ARM64_HELPER_FAULT_BAD_OPERAND,
                  0u,
                  DB_LINUX_ARM64_GUEST_ACCESS_NONE);
        return 0u;
    }

    cf = (state->rflags & DB_X64_RFLAGS_CF) != 0u;
    pf = (state->rflags & DB_X64_RFLAGS_PF) != 0u;
    zf = (state->rflags & DB_X64_RFLAGS_ZF) != 0u;
    sf = (state->rflags & DB_X64_RFLAGS_SF) != 0u;
    of = (state->rflags & DB_X64_RFLAGS_OF) != 0u;

    switch ((uint32_t)condition) {
    case 0u: result = of; break;
    case 1u: result = !of; break;
    case 2u: result = cf; break;
    case 3u: result = !cf; break;
    case 4u: result = zf; break;
    case 5u: result = !zf; break;
    case 6u: result = cf || zf; break;
    case 7u: result = !cf && !zf; break;
    case 8u: result = sf; break;
    case 9u: result = !sf; break;
    case 10u: result = pf; break;
    case 11u: result = !pf; break;
    case 12u: result = sf != of; break;
    case 13u: result = sf == of; break;
    case 14u: result = zf || sf != of; break;
    case 15u: result = !zf && sf == of; break;
    default: result = false; break;
    }
    return result ? UINT64_C(1) : UINT64_C(0);
}

#if defined(__aarch64__)
static uint64_t function_address(void (*function)(void))
{
    uintptr_t address = 0u;

    _Static_assert(sizeof(function) == sizeof(address),
                   "function pointer and uintptr_t sizes differ");
    (void)memcpy(&address, &function, sizeof(address));
    return (uint64_t)address;
}
#endif

db_linux_arm64_guest_runtime_status
 db_linux_arm64_guest_runtime_helper_targets(
    db_x64_arm64_helper_targets *targets)
{
    if (targets == NULL) return DB_LINUX_ARM64_GUEST_RUNTIME_NULL_ARGUMENT;

#if !defined(__aarch64__)
    return DB_LINUX_ARM64_GUEST_RUNTIME_UNSUPPORTED_HOST;
#else
    db_x64_arm64_helper_targets completed;
    uint32_t index;

    (void)memset(&completed, 0, sizeof(completed));
    completed.addresses[DB_X64_ARM64_HELPER_FORM_GUEST_ADDRESS] =
        function_address(db_linux_arm64_helper_form_guest_address);
    completed.addresses[DB_X64_ARM64_HELPER_READ_GUEST_U64] =
        function_address(db_linux_arm64_helper_read_guest_u64);
    completed.addresses[DB_X64_ARM64_HELPER_WRITE_GUEST_U64] =
        function_address(db_linux_arm64_helper_write_guest_u64);
    completed.addresses[DB_X64_ARM64_HELPER_PUSH_GUEST_U64] =
        function_address(db_linux_arm64_helper_push_guest_u64);
    completed.addresses[DB_X64_ARM64_HELPER_POP_GUEST_U64] =
        function_address(db_linux_arm64_helper_pop_guest_u64);
    completed.addresses[DB_X64_ARM64_HELPER_UPDATE_LOGIC_FLAGS] =
        function_address(db_linux_arm64_helper_update_logic_flags);
    completed.addresses[DB_X64_ARM64_HELPER_TEST_GUEST_CONDITION] =
        function_address(db_linux_arm64_helper_test_guest_condition);
    completed.addresses[DB_X64_ARM64_HELPER_DISPATCH_DIRECT] =
        db_linux_arm64_translation_terminal_helper_address(
            DB_TRANSLATED_TERMINAL_EXIT_DIRECT);
    completed.addresses[DB_X64_ARM64_HELPER_DISPATCH_TARGET] =
        db_linux_arm64_translation_terminal_helper_address(
            DB_TRANSLATED_TERMINAL_EXIT_TARGET);
    completed.addresses[DB_X64_ARM64_HELPER_DISPATCH_CONDITIONAL] =
        db_linux_arm64_translation_terminal_helper_address(
            DB_TRANSLATED_TERMINAL_EXIT_CONDITIONAL);
    completed.addresses[DB_X64_ARM64_HELPER_RAISE_TRAP] =
        db_linux_arm64_translation_terminal_helper_address(
            DB_TRANSLATED_TERMINAL_EXIT_TRAP);

    for (index = 1u; index < DB_X64_ARM64_HELPER_TARGET_COUNT; ++index) {
        if (completed.addresses[index] == 0u ||
            (completed.addresses[index] & UINT64_C(3)) != 0u) {
            return DB_LINUX_ARM64_GUEST_RUNTIME_BAD_MEMORY;
        }
    }
    *targets = completed;
    return DB_LINUX_ARM64_GUEST_RUNTIME_OK;
#endif
}

const char *db_linux_arm64_guest_runtime_status_name(
    db_linux_arm64_guest_runtime_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "bad guest memory descriptor",
        "noncanonical guest address",
        "guest address overflow",
        "guest address not mapped",
        "guest access denied",
        "unsupported host"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 guest runtime status";
    }
    return names[(size_t)status];
}
