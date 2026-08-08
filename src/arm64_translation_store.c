#include "dblyat/arm64_translation_store.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

struct db_store_region {
    const void *address;
    size_t size;
};

static bool db_store_zero_bytes(const void *data, size_t size)
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

static bool db_store_overlap(const void *left,
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

static bool db_store_multiply(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool db_store_power_of_two(size_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool db_store_x64_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return sign != 0u ? upper == UINT64_C(0xffff) : upper == 0u;
}

static bool db_store_handle_empty(const db_linux_arm64_wx_handle *handle)
{
    return handle->mapping == NULL &&
           handle->entry == NULL &&
           handle->allocation_size == 0u &&
           handle->state == (uint32_t)DB_LINUX_ARM64_WX_EMPTY &&
           handle->reserved == 0u;
}

static bool db_store_handle_owned(const db_linux_arm64_wx_handle *handle)
{
    return handle->mapping != NULL &&
           handle->entry != NULL &&
           handle->allocation_size != 0u &&
           handle->state == (uint32_t)DB_LINUX_ARM64_WX_PUBLISHED &&
           handle->reserved == 0u;
}

static bool db_store_handle_cleanup_pending(
    const db_linux_arm64_wx_handle *handle)
{
    return (db_store_handle_owned(handle) ||
            (handle->mapping != NULL &&
             handle->entry == NULL &&
             handle->allocation_size != 0u &&
             handle->state ==
                 (uint32_t)DB_LINUX_ARM64_WX_ROLLBACK_PENDING &&
             handle->reserved == 0u));
}

static bool db_store_regions_disjoint(const struct db_store_region *regions,
                                      size_t count)
{
    size_t left;

    for (left = 0u; left < count; ++left) {
        size_t right;

        if (regions[left].address == NULL || regions[left].size == 0u) {
            return false;
        }
        for (right = left + 1u; right < count; ++right) {
            if (db_store_overlap(regions[left].address,
                                 regions[left].size,
                                 regions[right].address,
                                 regions[right].size)) {
                return false;
            }
        }
    }
    return true;
}

static bool db_store_workspace_valid(
    const db_arm64_translation_workspace *workspace)
{
    struct db_store_region regions[9];
    size_t block_bytes;
    size_t ir_bytes;
    size_t selection_bytes;
    size_t plan_bytes;
    size_t source_word_bytes;
    size_t relocation_bytes;
    size_t output_word_bytes;
    size_t resolution_bytes;

    if (workspace == NULL ||
        workspace->block_records == NULL ||
        workspace->ir == NULL ||
        workspace->selections == NULL ||
        workspace->plans == NULL ||
        workspace->source_words == NULL ||
        workspace->relocations == NULL ||
        workspace->output_words == NULL ||
        workspace->resolutions == NULL ||
        workspace->image_bytes == NULL ||
        workspace->block_record_capacity == 0u ||
        workspace->ir_capacity == 0u ||
        workspace->selection_capacity == 0u ||
        workspace->plan_capacity == 0u ||
        workspace->source_word_capacity == 0u ||
        workspace->relocation_capacity == 0u ||
        workspace->output_word_capacity == 0u ||
        workspace->resolution_capacity == 0u ||
        workspace->image_capacity == 0u ||
        !db_store_zero_bytes(workspace->reserved,
                             sizeof(workspace->reserved)) ||
        !db_store_multiply(workspace->block_record_capacity,
                           sizeof(*workspace->block_records),
                           &block_bytes) ||
        !db_store_multiply(workspace->ir_capacity,
                           sizeof(*workspace->ir),
                           &ir_bytes) ||
        !db_store_multiply(workspace->selection_capacity,
                           sizeof(*workspace->selections),
                           &selection_bytes) ||
        !db_store_multiply(workspace->plan_capacity,
                           sizeof(*workspace->plans),
                           &plan_bytes) ||
        !db_store_multiply(workspace->source_word_capacity,
                           sizeof(*workspace->source_words),
                           &source_word_bytes) ||
        !db_store_multiply(workspace->relocation_capacity,
                           sizeof(*workspace->relocations),
                           &relocation_bytes) ||
        !db_store_multiply(workspace->output_word_capacity,
                           sizeof(*workspace->output_words),
                           &output_word_bytes) ||
        !db_store_multiply(workspace->resolution_capacity,
                           sizeof(*workspace->resolutions),
                           &resolution_bytes)) {
        return false;
    }

    regions[0] = (struct db_store_region){workspace->block_records,
                                           block_bytes};
    regions[1] = (struct db_store_region){workspace->ir, ir_bytes};
    regions[2] = (struct db_store_region){workspace->selections,
                                           selection_bytes};
    regions[3] = (struct db_store_region){workspace->plans, plan_bytes};
    regions[4] = (struct db_store_region){workspace->source_words,
                                           source_word_bytes};
    regions[5] = (struct db_store_region){workspace->relocations,
                                           relocation_bytes};
    regions[6] = (struct db_store_region){workspace->output_words,
                                           output_word_bytes};
    regions[7] = (struct db_store_region){workspace->resolutions,
                                           resolution_bytes};
    regions[8] = (struct db_store_region){workspace->image_bytes,
                                           workspace->image_capacity};
    return db_store_regions_disjoint(regions, 9u);
}

static bool db_store_config_valid(
    const db_arm64_translation_store_config *config)
{
    return config != NULL &&
           config->code != NULL &&
           config->helper_targets != NULL &&
           config->wx_platform != NULL &&
           config->reserve_code_base != NULL &&
           config->source_generation != 0u &&
           config->instruction_budget != 0u &&
           config->byte_budget != 0u &&
           config->page_size >= UINT64_C(4096) &&
           (config->page_size & (config->page_size - UINT64_C(1))) == 0u &&
           db_store_zero_bytes(config->reserved, sizeof(config->reserved));
}

static bool db_store_descriptor_shape_valid(
    const db_arm64_translation_store *store)
{
    size_t slot_bytes;
    size_t handle_bytes;

    if (store == NULL ||
        store->cache.slots == NULL ||
        store->handles == NULL ||
        store->owned == NULL ||
        !db_store_power_of_two(store->capacity) ||
        store->cache.capacity != store->capacity ||
        store->cache.count > store->capacity ||
        store->cache.format_version != DB_ARM64_BLOCK_CACHE_FORMAT_VERSION ||
        store->format_version != DB_ARM64_TRANSLATION_STORE_FORMAT_VERSION ||
        (store->state != (uint32_t)DB_ARM64_TRANSLATION_STORE_READY &&
         store->state != (uint32_t)DB_ARM64_TRANSLATION_STORE_POISONED) ||
        !db_store_zero_bytes(store->reserved, sizeof(store->reserved)) ||
        !db_store_zero_bytes(store->cache.reserved,
                             sizeof(store->cache.reserved)) ||
        !db_store_multiply(store->capacity,
                           sizeof(*store->cache.slots),
                           &slot_bytes) ||
        !db_store_multiply(store->capacity,
                           sizeof(*store->handles),
                           &handle_bytes) ||
        db_store_overlap(store, sizeof(*store),
                         store->cache.slots, slot_bytes) ||
        db_store_overlap(store, sizeof(*store),
                         store->handles, handle_bytes) ||
        db_store_overlap(store, sizeof(*store),
                         store->owned, store->capacity) ||
        db_store_overlap(store->cache.slots, slot_bytes,
                         store->handles, handle_bytes) ||
        db_store_overlap(store->cache.slots, slot_bytes,
                         store->owned, store->capacity) ||
        db_store_overlap(store->handles, handle_bytes,
                         store->owned, store->capacity)) {
        return false;
    }
    return true;
}

static bool db_store_ready_valid(const db_arm64_translation_store *store)
{
    size_t index;
    size_t owned_count = 0u;

    if (!db_store_descriptor_shape_valid(store) ||
        store->state != (uint32_t)DB_ARM64_TRANSLATION_STORE_READY ||
        !db_store_handle_empty(&store->rollback_handle)) {
        return false;
    }

    for (index = 0u; index < store->capacity; ++index) {
        const uint8_t owned = store->owned[index];
        const db_arm64_block_cache_slot *slot = &store->cache.slots[index];

        if (owned > 1u) {
            return false;
        }
        if (owned == 0u) {
            if (!db_store_handle_empty(&store->handles[index]) ||
                slot->state ==
                    (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED) {
                return false;
            }
            continue;
        }

        if (!db_store_handle_owned(&store->handles[index]) ||
            slot->state !=
                (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED) {
            return false;
        }
        ++owned_count;
    }

    return owned_count == store->cache.count;
}

static bool db_store_outputs_disjoint(
    const db_arm64_translation_store *store,
    const db_arm64_translation_workspace *workspace,
    const db_arm64_published_block *published,
    const db_arm64_translation_store_summary *summary)
{
    struct db_store_region regions[13];
    size_t slot_bytes;
    size_t handle_bytes;
    size_t block_bytes;
    size_t ir_bytes;
    size_t selection_bytes;
    size_t plan_bytes;
    size_t source_word_bytes;
    size_t relocation_bytes;
    size_t output_word_bytes;
    size_t resolution_bytes;

    if (!db_store_multiply(store->capacity,
                           sizeof(*store->cache.slots), &slot_bytes) ||
        !db_store_multiply(store->capacity,
                           sizeof(*store->handles), &handle_bytes) ||
        !db_store_multiply(workspace->block_record_capacity,
                           sizeof(*workspace->block_records), &block_bytes) ||
        !db_store_multiply(workspace->ir_capacity,
                           sizeof(*workspace->ir), &ir_bytes) ||
        !db_store_multiply(workspace->selection_capacity,
                           sizeof(*workspace->selections), &selection_bytes) ||
        !db_store_multiply(workspace->plan_capacity,
                           sizeof(*workspace->plans), &plan_bytes) ||
        !db_store_multiply(workspace->source_word_capacity,
                           sizeof(*workspace->source_words), &source_word_bytes) ||
        !db_store_multiply(workspace->relocation_capacity,
                           sizeof(*workspace->relocations), &relocation_bytes) ||
        !db_store_multiply(workspace->output_word_capacity,
                           sizeof(*workspace->output_words), &output_word_bytes) ||
        !db_store_multiply(workspace->resolution_capacity,
                           sizeof(*workspace->resolutions), &resolution_bytes)) {
        return false;
    }

    regions[0] = (struct db_store_region){published, sizeof(*published)};
    regions[1] = (struct db_store_region){summary, sizeof(*summary)};
    regions[2] = (struct db_store_region){store->cache.slots, slot_bytes};
    regions[3] = (struct db_store_region){store->handles, handle_bytes};
    regions[4] = (struct db_store_region){store->owned, store->capacity};
    regions[5] = (struct db_store_region){workspace->block_records, block_bytes};
    regions[6] = (struct db_store_region){workspace->ir, ir_bytes};
    regions[7] = (struct db_store_region){workspace->selections,
                                           selection_bytes};
    regions[8] = (struct db_store_region){workspace->plans, plan_bytes};
    regions[9] = (struct db_store_region){workspace->source_words,
                                           source_word_bytes};
    regions[10] = (struct db_store_region){workspace->relocations,
                                            relocation_bytes};
    regions[11] = (struct db_store_region){workspace->output_words,
                                            output_word_bytes};
    regions[12] = (struct db_store_region){workspace->resolutions,
                                            resolution_bytes};

    if (!db_store_regions_disjoint(regions, 13u)) {
        return false;
    }
    return !db_store_overlap(published, sizeof(*published),
                             workspace->image_bytes,
                             workspace->image_capacity) &&
           !db_store_overlap(summary, sizeof(*summary),
                             workspace->image_bytes,
                             workspace->image_capacity) &&
           !db_store_overlap(store->cache.slots, slot_bytes,
                             workspace->image_bytes,
                             workspace->image_capacity) &&
           !db_store_overlap(store->handles, handle_bytes,
                             workspace->image_bytes,
                             workspace->image_capacity) &&
           !db_store_overlap(store->owned, store->capacity,
                             workspace->image_bytes,
                             workspace->image_capacity);
}

static void db_store_poison_with_handle(
    db_arm64_translation_store *store,
    const db_linux_arm64_wx_handle *handle)
{
    store->rollback_handle = *handle;
    store->state = (uint32_t)DB_ARM64_TRANSLATION_STORE_POISONED;
}

static db_arm64_translation_store_status db_store_release_fresh(
    db_arm64_translation_store *store,
    const db_linux_arm64_wx_platform *platform,
    db_linux_arm64_wx_handle *handle)
{
    db_linux_arm64_wx_audit release_audit;
    db_linux_arm64_wx_status release_status;

    memset(&release_audit, 0, sizeof(release_audit));
    release_status = db_linux_arm64_wx_release(
        platform, handle, &release_audit);
    if (release_status != DB_LINUX_ARM64_WX_OK) {
        if (db_store_handle_cleanup_pending(handle)) {
            db_store_poison_with_handle(store, handle);
        } else {
            store->state =
                (uint32_t)DB_ARM64_TRANSLATION_STORE_POISONED;
        }
        return DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED;
    }
    return DB_ARM64_TRANSLATION_STORE_OK;
}

db_arm64_translation_store_status db_arm64_translation_store_init(
    db_arm64_translation_store *store,
    db_arm64_block_cache_slot *slots,
    db_linux_arm64_wx_handle *handles,
    uint8_t *owned,
    size_t capacity)
{
    db_arm64_translation_store completed;
    db_arm64_block_cache cache;
    size_t slot_bytes;
    size_t handle_bytes;

    if (store == NULL || slots == NULL || handles == NULL || owned == NULL) {
        return DB_ARM64_TRANSLATION_STORE_NULL_ARGUMENT;
    }
    if (!db_store_power_of_two(capacity) ||
        !db_store_multiply(capacity, sizeof(*slots), &slot_bytes) ||
        !db_store_multiply(capacity, sizeof(*handles), &handle_bytes)) {
        return DB_ARM64_TRANSLATION_STORE_BAD_CAPACITY;
    }
    if (db_store_overlap(store, sizeof(*store), slots, slot_bytes) ||
        db_store_overlap(store, sizeof(*store), handles, handle_bytes) ||
        db_store_overlap(store, sizeof(*store), owned, capacity) ||
        db_store_overlap(slots, slot_bytes, handles, handle_bytes) ||
        db_store_overlap(slots, slot_bytes, owned, capacity) ||
        db_store_overlap(handles, handle_bytes, owned, capacity)) {
        return DB_ARM64_TRANSLATION_STORE_BUFFER_OVERLAP;
    }

    memset(&cache, 0, sizeof(cache));
    if (db_arm64_block_cache_init(&cache, slots, capacity) !=
        DB_ARM64_BLOCK_CACHE_OK) {
        return DB_ARM64_TRANSLATION_STORE_BAD_CAPACITY;
    }

    memset(handles, 0, handle_bytes);
    memset(owned, 0, capacity);
    memset(&completed, 0, sizeof(completed));
    completed.cache = cache;
    completed.handles = handles;
    completed.owned = owned;
    completed.capacity = capacity;
    completed.state = (uint32_t)DB_ARM64_TRANSLATION_STORE_READY;
    completed.format_version = DB_ARM64_TRANSLATION_STORE_FORMAT_VERSION;
    *store = completed;
    return DB_ARM64_TRANSLATION_STORE_OK;
}

db_arm64_translation_store_status db_arm64_translation_store_fill(
    db_arm64_translation_store *store,
    const db_arm64_translation_store_config *config,
    db_arm64_translation_workspace *workspace,
    uint64_t guest_source_rip,
    db_arm64_published_block *published,
    db_arm64_translation_store_summary *summary)
{
    db_arm64_translation_store_summary completed;
    db_arm64_published_block cached;
    db_arm64_published_block candidate;
    db_arm64_block_cache_result lookup;
    db_arm64_block_cache_result insert;
    db_linux_arm64_wx_handle fresh_handle;
    db_linux_arm64_wx_audit wx_audit;
    db_arm64_block_cache_status cache_status;
    db_x64_block_status block_status;
    db_x64_ir_lower_status ir_status;
    db_arm64_select_status select_status;
    db_x64_arm64_plan_status plan_status;
    db_x64_arm64_encode_status encode_status;
    db_x64_arm64_relocate_status relocate_status;
    db_arm64_code_image_status image_status;
    db_linux_arm64_wx_status wx_status;
    db_arm64_published_block_status published_status;
    uint64_t code_base = 0u;
    size_t slot_index;

    if (store == NULL || config == NULL || workspace == NULL ||
        published == NULL || summary == NULL) {
        return DB_ARM64_TRANSLATION_STORE_NULL_ARGUMENT;
    }
    if (!db_store_ready_valid(store)) {
        return DB_ARM64_TRANSLATION_STORE_BAD_STORE;
    }
    if (!db_store_workspace_valid(workspace)) {
        return DB_ARM64_TRANSLATION_STORE_BAD_WORKSPACE;
    }
    if (!db_store_config_valid(config) ||
        guest_source_rip == 0u ||
        !db_store_x64_canonical(guest_source_rip)) {
        return DB_ARM64_TRANSLATION_STORE_BAD_CONFIGURATION;
    }
    if (!db_store_outputs_disjoint(store, workspace, published, summary)) {
        return DB_ARM64_TRANSLATION_STORE_BUFFER_OVERLAP;
    }

    memset(&completed, 0, sizeof(completed));
    completed.format_version = DB_ARM64_TRANSLATION_STORE_FORMAT_VERSION;
    memset(&cached, 0, sizeof(cached));
    memset(&lookup, 0, sizeof(lookup));

    cache_status = db_arm64_block_cache_lookup(
        &store->cache,
        guest_source_rip,
        config->source_generation,
        config->translation_key,
        &cached,
        &lookup);
    completed.cache_status = (uint32_t)cache_status;

    if (cache_status == DB_ARM64_BLOCK_CACHE_OK) {
        completed.cache_result = lookup;
        completed.outcome =
            (uint32_t)DB_ARM64_TRANSLATION_STORE_OUTCOME_HIT;
        *published = cached;
        *summary = completed;
        return DB_ARM64_TRANSLATION_STORE_OK;
    }
    if (cache_status != DB_ARM64_BLOCK_CACHE_NOT_FOUND) {
        return DB_ARM64_TRANSLATION_STORE_CACHE_FAILED;
    }

    if (config->reserve_code_base(
            config->code_base_context,
            guest_source_rip,
            config->source_generation,
            config->translation_key,
            config->page_size,
            &code_base) != 0 ||
        code_base == 0u ||
        code_base > (uint64_t)UINTPTR_MAX ||
        (code_base & (config->page_size - UINT64_C(1))) != 0u) {
        return DB_ARM64_TRANSLATION_STORE_CODE_BASE_FAILED;
    }
    completed.requested_code_base = code_base;

    block_status = db_x64_block_build(
        config->code,
        guest_source_rip,
        config->instruction_budget,
        config->byte_budget,
        workspace->block_records,
        workspace->block_record_capacity,
        &completed.block);
    completed.block_status = (uint32_t)block_status;
    if (block_status != DB_X64_BLOCK_OK) {
        return DB_ARM64_TRANSLATION_STORE_BLOCK_FAILED;
    }
    if (completed.block.stop_reason != DB_X64_BLOCK_STOP_TERMINATOR ||
        completed.block.record_count == 0u) {
        return DB_ARM64_TRANSLATION_STORE_BLOCK_NOT_TERMINATED;
    }

    ir_status = db_x64_ir_lower_block(
        workspace->block_records,
        completed.block.record_count,
        workspace->ir,
        workspace->ir_capacity,
        &completed.ir);
    completed.ir_status = (uint32_t)ir_status;
    if (ir_status != DB_X64_IR_LOWER_OK) {
        return DB_ARM64_TRANSLATION_STORE_IR_FAILED;
    }

    select_status = db_arm64_select_block(
        workspace->ir,
        completed.ir.instruction_count,
        workspace->selections,
        workspace->selection_capacity,
        &completed.select);
    completed.select_status = (uint32_t)select_status;
    if (select_status != DB_ARM64_SELECT_OK) {
        return DB_ARM64_TRANSLATION_STORE_SELECT_FAILED;
    }

    plan_status = db_x64_arm64_plan_block(
        workspace->selections,
        completed.select.operation_count,
        workspace->plans,
        workspace->plan_capacity,
        &completed.plan);
    completed.plan_status = (uint32_t)plan_status;
    if (plan_status != DB_X64_ARM64_PLAN_OK ||
        completed.plan.plan_count == 0u ||
        completed.plan.has_terminal != 1u) {
        return DB_ARM64_TRANSLATION_STORE_PLAN_FAILED;
    }

    encode_status = db_x64_arm64_encode_block(
        workspace->plans,
        completed.plan.plan_count,
        workspace->source_words,
        workspace->source_word_capacity,
        workspace->relocations,
        workspace->relocation_capacity,
        &completed.encode);
    completed.encode_status = (uint32_t)encode_status;
    if (encode_status != DB_X64_ARM64_ENCODE_OK) {
        return DB_ARM64_TRANSLATION_STORE_ENCODE_FAILED;
    }

    relocate_status = db_x64_arm64_resolve_helpers(
        workspace->source_words,
        completed.encode.word_count,
        workspace->relocations,
        completed.encode.relocation_count,
        config->helper_targets,
        code_base,
        workspace->output_words,
        workspace->output_word_capacity,
        workspace->resolutions,
        workspace->resolution_capacity,
        &completed.relocate);
    completed.relocate_status = (uint32_t)relocate_status;
    if (relocate_status != DB_X64_ARM64_RELOCATE_OK) {
        return DB_ARM64_TRANSLATION_STORE_RELOCATE_FAILED;
    }

    image_status = db_arm64_code_image_build(
        workspace->output_words,
        completed.relocate.output_word_count,
        code_base,
        0u,
        config->page_size,
        workspace->image_bytes,
        workspace->image_capacity,
        &completed.image);
    completed.image_status = (uint32_t)image_status;
    if (image_status != DB_ARM64_CODE_IMAGE_OK) {
        return DB_ARM64_TRANSLATION_STORE_IMAGE_FAILED;
    }

    db_linux_arm64_wx_handle_init(&fresh_handle);
    memset(&wx_audit, 0, sizeof(wx_audit));
    wx_status = db_linux_arm64_wx_publish(
        config->wx_platform,
        &completed.image,
        workspace->image_bytes,
        (size_t)completed.image.byte_count,
        &fresh_handle,
        &wx_audit);
    completed.wx = wx_audit;
    completed.wx_status = (uint32_t)wx_status;
    if (wx_status != DB_LINUX_ARM64_WX_OK) {
        if (wx_status == DB_LINUX_ARM64_WX_ROLLBACK_FAILED &&
            db_store_handle_cleanup_pending(&fresh_handle)) {
            db_store_poison_with_handle(store, &fresh_handle);
            return DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED;
        }
        return DB_ARM64_TRANSLATION_STORE_WX_FAILED;
    }

    memset(&candidate, 0, sizeof(candidate));
    published_status = db_arm64_published_block_build(
        &completed.block,
        &completed.image,
        &fresh_handle,
        &wx_audit,
        config->source_generation,
        config->translation_key,
        &candidate);
    completed.published_status = (uint32_t)published_status;
    if (published_status != DB_ARM64_PUBLISHED_BLOCK_OK) {
        if (db_store_release_fresh(store,
                                   config->wx_platform,
                                   &fresh_handle) !=
            DB_ARM64_TRANSLATION_STORE_OK) {
            return DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED;
        }
        return DB_ARM64_TRANSLATION_STORE_PUBLISHED_BLOCK_FAILED;
    }

    published_status = db_arm64_published_block_bind_terminal(
        &workspace->plans[completed.plan.plan_count - 1u],
        &candidate);
    completed.published_status = (uint32_t)published_status;
    if (published_status != DB_ARM64_PUBLISHED_BLOCK_OK) {
        if (db_store_release_fresh(store,
                                   config->wx_platform,
                                   &fresh_handle) !=
            DB_ARM64_TRANSLATION_STORE_OK) {
            return DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED;
        }
        return DB_ARM64_TRANSLATION_STORE_TERMINAL_BIND_FAILED;
    }

    memset(&insert, 0, sizeof(insert));
    cache_status = db_arm64_block_cache_insert(
        &store->cache, &candidate, &insert);
    completed.cache_status = (uint32_t)cache_status;
    if (cache_status != DB_ARM64_BLOCK_CACHE_OK || insert.inserted != 1u ||
        insert.found != 1u || insert.slot_index >= store->capacity) {
        if (db_store_release_fresh(store,
                                   config->wx_platform,
                                   &fresh_handle) !=
            DB_ARM64_TRANSLATION_STORE_OK) {
            return DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED;
        }
        return DB_ARM64_TRANSLATION_STORE_INSERT_FAILED;
    }

    slot_index = (size_t)insert.slot_index;
    if (store->owned[slot_index] != 0u ||
        !db_store_handle_empty(&store->handles[slot_index])) {
        store->state = (uint32_t)DB_ARM64_TRANSLATION_STORE_POISONED;
        store->rollback_handle = fresh_handle;
        return DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED;
    }

    store->handles[slot_index] = fresh_handle;
    store->owned[slot_index] = 1u;
    completed.cache_result = insert;
    completed.outcome =
        (uint32_t)DB_ARM64_TRANSLATION_STORE_OUTCOME_PUBLISHED;
    *published = candidate;
    *summary = completed;
    return DB_ARM64_TRANSLATION_STORE_OK;
}

db_arm64_translation_store_status db_arm64_translation_store_dispose(
    db_arm64_translation_store *store,
    const db_linux_arm64_wx_platform *platform)
{
    size_t index;
    size_t slot_bytes;
    size_t handle_bytes;
    bool failed = false;

    if (store == NULL || platform == NULL) {
        return DB_ARM64_TRANSLATION_STORE_NULL_ARGUMENT;
    }
    if (!db_store_descriptor_shape_valid(store) ||
        !db_store_multiply(store->capacity,
                           sizeof(*store->cache.slots), &slot_bytes) ||
        !db_store_multiply(store->capacity,
                           sizeof(*store->handles), &handle_bytes)) {
        return DB_ARM64_TRANSLATION_STORE_BAD_STORE;
    }

    store->state = (uint32_t)DB_ARM64_TRANSLATION_STORE_POISONED;

    for (index = 0u; index < store->capacity; ++index) {
        if (store->owned[index] == 0u) {
            continue;
        }
        if (!db_store_handle_cleanup_pending(&store->handles[index])) {
            failed = true;
            continue;
        }
        {
            db_linux_arm64_wx_audit audit;
            const db_linux_arm64_wx_status status =
                db_linux_arm64_wx_release(
                    platform, &store->handles[index], &audit);

            if (status != DB_LINUX_ARM64_WX_OK) {
                failed = true;
            } else {
                store->owned[index] = 0u;
            }
        }
    }

    if (!db_store_handle_empty(&store->rollback_handle)) {
        if (!db_store_handle_cleanup_pending(&store->rollback_handle)) {
            failed = true;
        } else {
            db_linux_arm64_wx_audit audit;
            const db_linux_arm64_wx_status status =
                db_linux_arm64_wx_release(
                    platform, &store->rollback_handle, &audit);

            if (status != DB_LINUX_ARM64_WX_OK) {
                failed = true;
            }
        }
    }

    if (failed) {
        return DB_ARM64_TRANSLATION_STORE_RELEASE_FAILED;
    }

    memset(store->cache.slots, 0, slot_bytes);
    memset(store->handles, 0, handle_bytes);
    memset(store->owned, 0, store->capacity);
    memset(store, 0, sizeof(*store));
    return DB_ARM64_TRANSLATION_STORE_OK;
}

const char *db_arm64_translation_store_status_name(
    db_arm64_translation_store_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad capacity",
        "bad store",
        "bad workspace",
        "bad configuration",
        "cache failed",
        "code base failed",
        "block build failed",
        "block did not terminate",
        "IR lowering failed",
        "ARM64 selection failed",
        "ARM64 planning failed",
        "ARM64 encoding failed",
        "ARM64 relocation failed",
        "ARM64 image failed",
        "W^X publication failed",
        "published block failed",
        "terminal binding failed",
        "cache insertion failed",
        "rollback failed",
        "release failed"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 translation store status";
    }
    return names[(size_t)status];
}

const char *db_arm64_translation_store_state_name(
    db_arm64_translation_store_state state)
{
    static const char *const names[] = {
        "empty",
        "ready",
        "poisoned"
    };

    if ((size_t)state >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 translation store state";
    }
    return names[(size_t)state];
}

const char *db_arm64_translation_store_outcome_name(
    db_arm64_translation_store_outcome outcome)
{
    static const char *const names[] = {
        "none",
        "cache hit",
        "published"
    };

    if ((size_t)outcome >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 translation store outcome";
    }
    return names[(size_t)outcome];
}
