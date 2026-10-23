#include "dblyat/arm64_published_block.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static bool zero_bytes(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t index;

    for (index = 0u; index < size; ++index) {
        if (bytes[index] != 0u) {
            return false;
        }
    }

    return true;
}

static bool regions_overlap(const void *left,
                            size_t left_size,
                            const void *right,
                            size_t right_size)
{
    const uintptr_t left_address = (uintptr_t)left;
    const uintptr_t right_address = (uintptr_t)right;

    if (left_size == 0u || right_size == 0u) {
        return false;
    }

    if (left_address <= right_address) {
        return right_address - left_address < left_size;
    }

    return left_address - right_address < right_size;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (UINT64_MAX - left < right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool round_up_u64(uint64_t value,
                         uint64_t alignment,
                         uint64_t *result)
{
    const uint64_t mask = alignment - UINT64_C(1);

    if (UINT64_MAX - value < mask) {
        return false;
    }

    *result = (value + mask) & ~mask;
    return true;
}

static bool x64_address_is_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return sign == 0u ? upper == 0u : upper == UINT64_C(0xffff);
}

static bool block_valid(const db_x64_block_summary *block,
                        uint64_t *guest_end)
{
    uint64_t end;

    if (block->source_rip == 0u ||
        !x64_address_is_canonical(block->source_rip) ||
        block->record_count == 0u ||
        block->byte_count == 0u ||
        block->record_count > block->instruction_budget ||
        block->record_count > block->output_capacity ||
        block->byte_count > block->byte_budget ||
        block->stop_reason < DB_X64_BLOCK_STOP_TERMINATOR ||
        block->stop_reason > DB_X64_BLOCK_STOP_ADDRESS_INVALID ||
        block->scalar_decode_status < DB_X64_DECODE_OK ||
        block->scalar_decode_status > DB_X64_DECODE_INVALID_ENCODING ||
        block->memory_decode_status < DB_X64_MEMORY_INSTRUCTION_OK ||
        block->memory_decode_status >
            DB_X64_MEMORY_INSTRUCTION_INVALID_ENCODING ||
        !zero_bytes(block->reserved, sizeof(block->reserved)) ||
        !add_u64(block->source_rip, block->byte_count, &end) ||
        !x64_address_is_canonical(end) ||
        (block->final_fallthrough_rip != 0u &&
         !x64_address_is_canonical(block->final_fallthrough_rip))) {
        return false;
    }

    *guest_end = end;
    return true;
}

static bool plan_valid(const db_arm64_code_image_plan *plan)
{
    uint64_t byte_count;
    uint64_t entry_offset;
    uint64_t entry_address;
    uint64_t image_end;
    uint64_t allocation_size;
    uint64_t allocation_end;

    if (plan->format_version != DB_ARM64_CODE_IMAGE_FORMAT_VERSION ||
        plan->word_count == 0u ||
        plan->page_size < UINT64_C(4096) ||
        (plan->page_size & (plan->page_size - UINT64_C(1))) != 0u ||
        plan->code_base == 0u ||
        (plan->code_base & (plan->page_size - UINT64_C(1))) != 0u ||
        plan->entry_word_index >= plan->word_count ||
        plan->instruction_cache_offset != 0u ||
        plan->initial_permissions !=
            (DB_ARM64_CODE_PERMISSION_READ |
             DB_ARM64_CODE_PERMISSION_WRITE) ||
        plan->final_permissions !=
            (DB_ARM64_CODE_PERMISSION_READ |
             DB_ARM64_CODE_PERMISSION_EXECUTE) ||
        !zero_bytes(plan->reserved, sizeof(plan->reserved))) {
        return false;
    }

    byte_count = (uint64_t)plan->word_count * UINT64_C(4);
    entry_offset = (uint64_t)plan->entry_word_index * UINT64_C(4);

    if (byte_count == 0u ||
        !round_up_u64(byte_count, plan->page_size, &allocation_size) ||
        allocation_size == 0u ||
        !add_u64(plan->code_base, entry_offset, &entry_address) ||
        !add_u64(plan->code_base, byte_count - UINT64_C(1), &image_end) ||
        !add_u64(plan->code_base,
                 allocation_size - UINT64_C(1),
                 &allocation_end) ||
        plan->byte_count != byte_count ||
        plan->entry_byte_offset != entry_offset ||
        plan->entry_address != entry_address ||
        plan->image_end_address != image_end ||
        plan->allocation_size != allocation_size ||
        plan->allocation_end_address != allocation_end ||
        plan->instruction_cache_size != byte_count) {
        return false;
    }

    return true;
}

static bool handle_valid(const db_arm64_code_image_plan *plan,
                         const db_linux_arm64_wx_handle *handle)
{
    return handle->mapping != NULL &&
           handle->entry != NULL &&
           handle->allocation_size != 0u &&
           handle->state == DB_LINUX_ARM64_WX_PUBLISHED &&
           handle->reserved == 0u &&
           plan->allocation_size <= SIZE_MAX &&
           (uint64_t)(uintptr_t)handle->mapping == plan->code_base &&
           (uint64_t)(uintptr_t)handle->entry == plan->entry_address &&
           handle->allocation_size == (size_t)plan->allocation_size;
}

static bool audit_valid(const db_arm64_code_image_plan *plan,
                        const db_linux_arm64_wx_handle *handle,
                        const db_linux_arm64_wx_audit *audit)
{
    uint64_t icache_address;

    if (!add_u64(plan->code_base,
                 plan->instruction_cache_offset,
                 &icache_address)) {
        return false;
    }

    return audit->version == DB_LINUX_ARM64_WX_AUDIT_VERSION &&
           audit->completed_state == DB_LINUX_ARM64_WX_PUBLISHED &&
           audit->failure_state == DB_LINUX_ARM64_WX_EMPTY &&
           audit->status == DB_LINUX_ARM64_WX_OK &&
           audit->primary_status == DB_LINUX_ARM64_WX_OK &&
           audit->requested_base == plan->code_base &&
           audit->mapped_base == (uint64_t)(uintptr_t)handle->mapping &&
           audit->entry_address == plan->entry_address &&
           audit->allocation_size == plan->allocation_size &&
           audit->image_size == plan->byte_count &&
           audit->icache_address == icache_address &&
           audit->icache_size == plan->instruction_cache_size &&
           audit->map_observed == 1u &&
           audit->copy_verified == 1u &&
           audit->rx_observed == 1u &&
           audit->icache_observed == 1u &&
           audit->rollback_observed == 0u &&
           audit->release_observed == 0u &&
           zero_bytes(audit->reserved, sizeof(audit->reserved));
}

static bool published_binding_base_valid(
    const db_arm64_published_block *published)
{
    uint64_t expected_end;

    return published->format_version ==
               DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION &&
           published->guest_source_rip != 0u &&
           x64_address_is_canonical(published->guest_source_rip) &&
           published->guest_byte_count != 0u &&
           published->guest_instruction_count != 0u &&
           published->block_stop_reason ==
               (uint32_t)DB_X64_BLOCK_STOP_TERMINATOR &&
           add_u64(published->guest_source_rip,
                   (uint64_t)published->guest_byte_count,
                   &expected_end) &&
           expected_end == published->guest_end_rip &&
           x64_address_is_canonical(published->guest_end_rip) &&
           (published->guest_fallthrough_rip == 0u ||
            x64_address_is_canonical(
                published->guest_fallthrough_rip)) &&
           zero_bytes(&published->terminal_exit,
                      sizeof(published->terminal_exit)) &&
           zero_bytes(published->reserved,
                      sizeof(published->reserved));
}

static bool terminal_plan_common_valid(
    const db_x64_arm64_plan_record *terminal_plan,
    const db_arm64_published_block *published)
{
    return terminal_plan->opcode == DB_X64_ARM64_PLAN_TAIL_HELPER &&
           terminal_plan->terminal == 1u &&
           terminal_plan->source_rip >= published->guest_source_rip &&
           terminal_plan->source_rip < published->guest_end_rip &&
           x64_address_is_canonical(terminal_plan->source_rip) &&
           terminal_plan->displacement == 0 &&
           terminal_plan->guest_state_offset == 0u &&
           terminal_plan->width == DB_IR_WIDTH_NONE &&
           terminal_plan->address_mode == DB_IR_ADDRESS_NONE &&
           terminal_plan->immediate16 == 0u &&
           terminal_plan->shift == 0u &&
           terminal_plan->host_destination == DB_X64_ARM64_HOST_NONE &&
           terminal_plan->host_auxiliary == DB_X64_ARM64_HOST_NONE &&
           terminal_plan->guest_register == DB_X64_ARM64_GUEST_NONE &&
           terminal_plan->base_guest_register == DB_X64_ARM64_GUEST_NONE &&
           terminal_plan->index_guest_register == DB_X64_ARM64_GUEST_NONE &&
           terminal_plan->scale == 1u &&
           zero_bytes(terminal_plan->reserved,
                      sizeof(terminal_plan->reserved));
}

db_arm64_published_block_status db_arm64_published_block_build(
    const db_x64_block_summary *block,
    const db_arm64_code_image_plan *plan,
    const db_linux_arm64_wx_handle *handle,
    const db_linux_arm64_wx_audit *audit,
    uint64_t source_generation,
    uint64_t translation_key,
    db_arm64_published_block *published)
{
    db_arm64_published_block completed;
    uint64_t guest_end;

    if (block == NULL || plan == NULL || handle == NULL ||
        audit == NULL || published == NULL) {
        return DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT;
    }

    if (regions_overlap(published, sizeof(*published),
                        block, sizeof(*block)) ||
        regions_overlap(published, sizeof(*published),
                        plan, sizeof(*plan)) ||
        regions_overlap(published, sizeof(*published),
                        handle, sizeof(*handle)) ||
        regions_overlap(published, sizeof(*published),
                        audit, sizeof(*audit))) {
        return DB_ARM64_PUBLISHED_BLOCK_BUFFER_OVERLAP;
    }

    if (source_generation == 0u) {
        return DB_ARM64_PUBLISHED_BLOCK_BAD_SOURCE_GENERATION;
    }

    if (!block_valid(block, &guest_end)) {
        return DB_ARM64_PUBLISHED_BLOCK_BAD_BLOCK;
    }

    if (!plan_valid(plan)) {
        return DB_ARM64_PUBLISHED_BLOCK_BAD_IMAGE_PLAN;
    }

    if (!handle_valid(plan, handle)) {
        return DB_ARM64_PUBLISHED_BLOCK_BAD_HANDLE;
    }

    if (!audit_valid(plan, handle, audit)) {
        return DB_ARM64_PUBLISHED_BLOCK_BAD_AUDIT;
    }

    memset(&completed, 0, sizeof(completed));
    completed.guest_source_rip = block->source_rip;
    completed.guest_end_rip = guest_end;
    completed.guest_fallthrough_rip = block->final_fallthrough_rip;
    completed.source_generation = source_generation;
    completed.translation_key = translation_key;
    completed.host_mapping_address = (uint64_t)(uintptr_t)handle->mapping;
    completed.host_entry_address = (uint64_t)(uintptr_t)handle->entry;
    completed.host_allocation_end = plan->allocation_end_address;
    completed.host_allocation_size = plan->allocation_size;
    completed.arm64_image_byte_count = plan->byte_count;
    completed.guest_byte_count = block->byte_count;
    completed.guest_instruction_count = block->record_count;
    completed.arm64_word_count = plan->word_count;
    completed.block_stop_reason = (uint32_t)block->stop_reason;
    completed.format_version = DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION;

    *published = completed;
    return DB_ARM64_PUBLISHED_BLOCK_OK;
}

db_arm64_published_block_status db_arm64_published_block_bind_terminal(
    const db_x64_arm64_plan_record *terminal_plan,
    db_arm64_published_block *published)
{
    db_arm64_published_block completed;
    db_translated_terminal_exit exit_record;
    db_translated_terminal_exit_status exit_status;

    if (terminal_plan == NULL || published == NULL) {
        return DB_ARM64_PUBLISHED_BLOCK_NULL_ARGUMENT;
    }

    if (regions_overlap(terminal_plan, sizeof(*terminal_plan),
                        published, sizeof(*published))) {
        return DB_ARM64_PUBLISHED_BLOCK_BUFFER_OVERLAP;
    }

    if (!zero_bytes(&published->terminal_exit,
                    sizeof(published->terminal_exit))) {
        return DB_ARM64_PUBLISHED_BLOCK_TERMINAL_ALREADY_BOUND;
    }

    if (!published_binding_base_valid(published) ||
        !terminal_plan_common_valid(terminal_plan, published)) {
        return DB_ARM64_PUBLISHED_BLOCK_BAD_TERMINAL_PLAN;
    }

    switch (terminal_plan->helper) {
    case DB_X64_ARM64_HELPER_DISPATCH_DIRECT:
        if (terminal_plan->host_source != DB_X64_ARM64_HOST_NONE ||
            terminal_plan->condition !=
                (db_ir_condition)DB_IR_CONDITION_NONE ||
            terminal_plan->trap != DB_IR_TRAP_NONE) {
            return DB_ARM64_PUBLISHED_BLOCK_BAD_TERMINAL_PLAN;
        }
        exit_status = db_translated_terminal_exit_plan_direct(
            terminal_plan->source_rip,
            terminal_plan->value,
            &exit_record);
        break;

    case DB_X64_ARM64_HELPER_DISPATCH_TARGET:
        if (terminal_plan->host_source != DB_X64_ARM64_HOST_TARGET ||
            terminal_plan->value != 0u ||
            terminal_plan->condition !=
                (db_ir_condition)DB_IR_CONDITION_NONE ||
            terminal_plan->trap != DB_IR_TRAP_NONE) {
            return DB_ARM64_PUBLISHED_BLOCK_BAD_TERMINAL_PLAN;
        }
        exit_status = db_translated_terminal_exit_plan_target(
            terminal_plan->source_rip,
            &exit_record);
        break;

    case DB_X64_ARM64_HELPER_DISPATCH_CONDITIONAL:
        if (terminal_plan->host_source != DB_X64_ARM64_HOST_TARGET ||
            terminal_plan->condition < DB_IR_CONDITION_OVERFLOW ||
            terminal_plan->condition > DB_IR_CONDITION_GREATER ||
            terminal_plan->trap != DB_IR_TRAP_NONE ||
            published->guest_fallthrough_rip == 0u) {
            return DB_ARM64_PUBLISHED_BLOCK_BAD_TERMINAL_PLAN;
        }
        exit_status = db_translated_terminal_exit_plan_conditional(
            terminal_plan->source_rip,
            terminal_plan->value,
            published->guest_fallthrough_rip,
            (uint32_t)terminal_plan->condition,
            &exit_record);
        break;

    case DB_X64_ARM64_HELPER_RAISE_TRAP:
        if (terminal_plan->host_source != DB_X64_ARM64_HOST_NONE ||
            terminal_plan->value != 0u ||
            terminal_plan->condition !=
                (db_ir_condition)DB_IR_CONDITION_NONE ||
            terminal_plan->trap <= DB_IR_TRAP_NONE ||
            terminal_plan->trap > DB_IR_TRAP_SYSCALL) {
            return DB_ARM64_PUBLISHED_BLOCK_BAD_TERMINAL_PLAN;
        }
        exit_status = db_translated_terminal_exit_plan_trap(
            terminal_plan->source_rip,
            (uint32_t)terminal_plan->trap,
            &exit_record);
        break;

    default:
        return DB_ARM64_PUBLISHED_BLOCK_BAD_TERMINAL_PLAN;
    }

    if (exit_status != DB_TRANSLATED_TERMINAL_EXIT_OK) {
        return DB_ARM64_PUBLISHED_BLOCK_BAD_TERMINAL_PLAN;
    }

    completed = *published;
    completed.terminal_exit = exit_record;
    *published = completed;
    return DB_ARM64_PUBLISHED_BLOCK_OK;
}

const char *db_arm64_published_block_status_name(
    db_arm64_published_block_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad source generation",
        "bad x86-64 block summary",
        "bad ARM64 code image plan",
        "bad W^X publication handle",
        "bad W^X publication audit",
        "bad translated terminal plan",
        "translated terminal exit already bound"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 published block status";
    }

    return names[(size_t)status];
}
