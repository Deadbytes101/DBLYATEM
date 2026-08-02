#include "dblyat/arm64_block_cache.h"

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

static bool multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0u && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (UINT64_MAX - left < right) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool x64_address_is_canonical(uint64_t address)
{
    const uint64_t upper = address >> 48u;
    const uint64_t sign = (address >> 47u) & UINT64_C(1);

    return sign == 0u ? upper == 0u : upper == UINT64_C(0xffff);
}

static bool key_valid(uint64_t guest_source_rip,
                      uint64_t source_generation)
{
    return guest_source_rip != 0u &&
           x64_address_is_canonical(guest_source_rip) &&
           source_generation != 0u;
}

static bool block_valid(const db_arm64_published_block *block)
{
    uint64_t guest_end;
    uint64_t allocation_end;
    uint64_t image_end;
    uint64_t image_byte_count;

    if (block->format_version !=
            DB_ARM64_PUBLISHED_BLOCK_FORMAT_VERSION ||
        !key_valid(block->guest_source_rip,
                   block->source_generation) ||
        block->guest_byte_count == 0u ||
        block->guest_instruction_count == 0u ||
        block->guest_instruction_count > block->guest_byte_count ||
        block->arm64_word_count == 0u ||
        block->block_stop_reason >
            (uint32_t)DB_X64_BLOCK_STOP_ADDRESS_INVALID ||
        block->host_mapping_address == 0u ||
        block->host_entry_address == 0u ||
        block->host_allocation_size == 0u ||
        (block->host_mapping_address & UINT64_C(4095)) != 0u ||
        (block->host_entry_address & UINT64_C(3)) != 0u ||
        (block->host_allocation_size & UINT64_C(4095)) != 0u ||
        !zero_bytes(block->reserved, sizeof(block->reserved))) {
        return false;
    }

    image_byte_count =
        (uint64_t)block->arm64_word_count * UINT64_C(4);

    if (block->arm64_image_byte_count != image_byte_count ||
        image_byte_count > block->host_allocation_size ||
        !add_u64(block->guest_source_rip,
                 (uint64_t)block->guest_byte_count,
                 &guest_end) ||
        guest_end != block->guest_end_rip ||
        !x64_address_is_canonical(guest_end) ||
        (block->guest_fallthrough_rip != 0u &&
         !x64_address_is_canonical(
             block->guest_fallthrough_rip)) ||
        !add_u64(block->host_mapping_address,
                 block->host_allocation_size - UINT64_C(1),
                 &allocation_end) ||
        allocation_end != block->host_allocation_end ||
        !add_u64(block->host_mapping_address,
                 image_byte_count - UINT64_C(1),
                 &image_end) ||
        image_end > allocation_end ||
        block->host_entry_address < block->host_mapping_address ||
        block->host_entry_address > image_end) {
        return false;
    }

    return true;
}

static uint64_t mix_u64(uint64_t value)
{
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31u;
    return value;
}

static uint64_t rotate_left(uint64_t value, unsigned int amount)
{
    return (value << amount) | (value >> (64u - amount));
}

static size_t key_home(uint64_t guest_source_rip,
                       uint64_t source_generation,
                       uint64_t translation_key,
                       size_t capacity)
{
    const uint64_t hash =
        mix_u64(guest_source_rip) ^
        rotate_left(mix_u64(source_generation), 21u) ^
        rotate_left(mix_u64(translation_key), 42u);

    return (size_t)(hash & (uint64_t)(capacity - 1u));
}

static bool key_equal(const db_arm64_published_block *block,
                      uint64_t guest_source_rip,
                      uint64_t source_generation,
                      uint64_t translation_key)
{
    return block->guest_source_rip == guest_source_rip &&
           block->source_generation == source_generation &&
           block->translation_key == translation_key;
}

static bool slot_empty_valid(const db_arm64_block_cache_slot *slot)
{
    return zero_bytes(slot, sizeof(*slot));
}

static bool slot_occupied_valid(const db_arm64_block_cache_slot *slot)
{
    return slot->state ==
               (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED &&
           slot->format_version ==
               DB_ARM64_BLOCK_CACHE_FORMAT_VERSION &&
           zero_bytes(slot->reserved, sizeof(slot->reserved)) &&
           block_valid(&slot->block);
}

static bool cache_descriptor_valid(const db_arm64_block_cache *cache,
                                   size_t *slot_bytes)
{
    if (cache->slots == NULL ||
        cache->capacity == 0u ||
        (cache->capacity & (cache->capacity - 1u)) != 0u ||
        cache->count > cache->capacity ||
        cache->format_version != DB_ARM64_BLOCK_CACHE_FORMAT_VERSION ||
        !zero_bytes(cache->reserved, sizeof(cache->reserved)) ||
        !multiply_size(cache->capacity,
                       sizeof(*cache->slots),
                       slot_bytes) ||
        regions_overlap(cache, sizeof(*cache),
                        cache->slots, *slot_bytes)) {
        return false;
    }

    return true;
}

static bool probe_chain_valid(const db_arm64_block_cache *cache,
                              size_t occupied_index)
{
    const db_arm64_published_block *block =
        &cache->slots[occupied_index].block;
    const size_t mask = cache->capacity - 1u;
    const size_t home = key_home(block->guest_source_rip,
                                 block->source_generation,
                                 block->translation_key,
                                 cache->capacity);
    size_t probe;

    for (probe = 0u; probe < cache->capacity; ++probe) {
        const size_t index = (home + probe) & mask;
        const db_arm64_block_cache_slot *slot =
            &cache->slots[index];

        if (index == occupied_index) {
            return true;
        }

        if (slot->state ==
                (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_EMPTY &&
            slot_empty_valid(slot)) {
            return false;
        }

        if (slot->state !=
                (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED ||
            !slot_occupied_valid(slot)) {
            return false;
        }
    }

    return false;
}

static bool cache_full_valid(const db_arm64_block_cache *cache,
                             size_t *slot_bytes)
{
    size_t occupied = 0u;
    size_t left;

    if (!cache_descriptor_valid(cache, slot_bytes)) {
        return false;
    }

    for (left = 0u; left < cache->capacity; ++left) {
        const db_arm64_block_cache_slot *slot =
            &cache->slots[left];
        size_t right;

        if (slot->state ==
            (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_EMPTY) {
            if (!slot_empty_valid(slot)) {
                return false;
            }
            continue;
        }

        if (!slot_occupied_valid(slot) ||
            !probe_chain_valid(cache, left)) {
            return false;
        }

        ++occupied;

        for (right = left + 1u;
             right < cache->capacity;
             ++right) {
            const db_arm64_block_cache_slot *other =
                &cache->slots[right];

            if (other->state ==
                (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED) {
                if (!slot_occupied_valid(other) ||
                    key_equal(&other->block,
                              slot->block.guest_source_rip,
                              slot->block.source_generation,
                              slot->block.translation_key)) {
                    return false;
                }
            }
        }
    }

    return occupied == cache->count;
}

static db_arm64_block_cache_result completed_result(size_t index,
                                                     size_t probes,
                                                     uint32_t inserted)
{
    db_arm64_block_cache_result result;

    memset(&result, 0, sizeof(result));
    result.slot_index = (uint64_t)index;
    result.probe_count = (uint64_t)probes;
    result.found = 1u;
    result.inserted = inserted;
    return result;
}

db_arm64_block_cache_status db_arm64_block_cache_init(
    db_arm64_block_cache *cache,
    db_arm64_block_cache_slot *slots,
    size_t capacity)
{
    db_arm64_block_cache completed;
    size_t slot_bytes;

    if (cache == NULL || slots == NULL) {
        return DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT;
    }

    if (capacity == 0u ||
        (capacity & (capacity - 1u)) != 0u ||
        !multiply_size(capacity, sizeof(*slots), &slot_bytes)) {
        return DB_ARM64_BLOCK_CACHE_BAD_CAPACITY;
    }

    if (regions_overlap(cache, sizeof(*cache),
                        slots, slot_bytes)) {
        return DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP;
    }

    memset(&completed, 0, sizeof(completed));
    completed.slots = slots;
    completed.capacity = capacity;
    completed.format_version =
        DB_ARM64_BLOCK_CACHE_FORMAT_VERSION;

    memset(slots, 0, slot_bytes);
    *cache = completed;
    return DB_ARM64_BLOCK_CACHE_OK;
}

db_arm64_block_cache_status db_arm64_block_cache_lookup(
    const db_arm64_block_cache *cache,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key,
    db_arm64_published_block *published,
    db_arm64_block_cache_result *result)
{
    size_t slot_bytes;
    size_t home;
    size_t probe;
    size_t mask;

    if (cache == NULL || published == NULL || result == NULL) {
        return DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT;
    }

    if (!key_valid(guest_source_rip, source_generation)) {
        return DB_ARM64_BLOCK_CACHE_BAD_KEY;
    }

    if (!cache_descriptor_valid(cache, &slot_bytes)) {
        return DB_ARM64_BLOCK_CACHE_BAD_CACHE;
    }

    if (regions_overlap(published, sizeof(*published),
                        result, sizeof(*result)) ||
        regions_overlap(cache, sizeof(*cache),
                        published, sizeof(*published)) ||
        regions_overlap(cache, sizeof(*cache),
                        result, sizeof(*result)) ||
        regions_overlap(cache->slots, slot_bytes,
                        published, sizeof(*published)) ||
        regions_overlap(cache->slots, slot_bytes,
                        result, sizeof(*result))) {
        return DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP;
    }

    if (!cache_full_valid(cache, &slot_bytes)) {
        return DB_ARM64_BLOCK_CACHE_BAD_CACHE;
    }

    home = key_home(guest_source_rip,
                    source_generation,
                    translation_key,
                    cache->capacity);
    mask = cache->capacity - 1u;

    for (probe = 0u; probe < cache->capacity; ++probe) {
        const size_t index = (home + probe) & mask;
        const db_arm64_block_cache_slot *slot =
            &cache->slots[index];

        if (slot->state ==
            (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_EMPTY) {
            if (!slot_empty_valid(slot)) {
                return DB_ARM64_BLOCK_CACHE_BAD_CACHE;
            }
            return DB_ARM64_BLOCK_CACHE_NOT_FOUND;
        }

        if (!slot_occupied_valid(slot)) {
            return DB_ARM64_BLOCK_CACHE_BAD_CACHE;
        }

        if (key_equal(&slot->block,
                      guest_source_rip,
                      source_generation,
                      translation_key)) {
            const db_arm64_published_block completed = slot->block;
            const db_arm64_block_cache_result summary =
                completed_result(index, probe + 1u, 0u);

            *published = completed;
            *result = summary;
            return DB_ARM64_BLOCK_CACHE_OK;
        }
    }

    return DB_ARM64_BLOCK_CACHE_NOT_FOUND;
}

db_arm64_block_cache_status db_arm64_block_cache_insert(
    db_arm64_block_cache *cache,
    const db_arm64_published_block *published,
    db_arm64_block_cache_result *result)
{
    db_arm64_block_cache_slot completed_slot;
    db_arm64_block_cache_result summary;
    size_t slot_bytes;
    size_t home;
    size_t probe;
    size_t mask;

    if (cache == NULL || published == NULL || result == NULL) {
        return DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT;
    }

    if (!cache_descriptor_valid(cache, &slot_bytes)) {
        return DB_ARM64_BLOCK_CACHE_BAD_CACHE;
    }

    if (regions_overlap(published, sizeof(*published),
                        result, sizeof(*result)) ||
        regions_overlap(cache, sizeof(*cache),
                        published, sizeof(*published)) ||
        regions_overlap(cache, sizeof(*cache),
                        result, sizeof(*result)) ||
        regions_overlap(cache->slots, slot_bytes,
                        published, sizeof(*published)) ||
        regions_overlap(cache->slots, slot_bytes,
                        result, sizeof(*result))) {
        return DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP;
    }

    if (!block_valid(published)) {
        return DB_ARM64_BLOCK_CACHE_BAD_BLOCK;
    }

    if (!cache_full_valid(cache, &slot_bytes)) {
        return DB_ARM64_BLOCK_CACHE_BAD_CACHE;
    }

    home = key_home(published->guest_source_rip,
                    published->source_generation,
                    published->translation_key,
                    cache->capacity);
    mask = cache->capacity - 1u;

    for (probe = 0u; probe < cache->capacity; ++probe) {
        const size_t index = (home + probe) & mask;
        db_arm64_block_cache_slot *slot = &cache->slots[index];

        if (slot->state ==
            (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_EMPTY) {
            memset(&completed_slot, 0, sizeof(completed_slot));
            completed_slot.block = *published;
            completed_slot.state =
                (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED;
            completed_slot.format_version =
                DB_ARM64_BLOCK_CACHE_FORMAT_VERSION;
            summary = completed_result(index, probe + 1u, 1u);

            *slot = completed_slot;
            cache->count += 1u;
            *result = summary;
            return DB_ARM64_BLOCK_CACHE_OK;
        }

        if (key_equal(&slot->block,
                      published->guest_source_rip,
                      published->source_generation,
                      published->translation_key)) {
            if (memcmp(&slot->block,
                       published,
                       sizeof(*published)) != 0) {
                return DB_ARM64_BLOCK_CACHE_KEY_CONFLICT;
            }

            summary = completed_result(index, probe + 1u, 0u);
            *result = summary;
            return DB_ARM64_BLOCK_CACHE_OK;
        }
    }

    return DB_ARM64_BLOCK_CACHE_FULL;
}

const char *db_arm64_block_cache_status_name(
    db_arm64_block_cache_status status)
{
    static const char *const names[] = {
        "ok",
        "null argument",
        "buffer overlap",
        "bad capacity",
        "bad cache",
        "bad key",
        "bad published block",
        "key conflict",
        "cache full",
        "not found"
    };

    if ((size_t)status >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 block cache status";
    }

    return names[(size_t)status];
}

const char *db_arm64_block_cache_slot_state_name(
    db_arm64_block_cache_slot_state state)
{
    static const char *const names[] = {
        "empty",
        "occupied"
    };

    if ((size_t)state >= sizeof(names) / sizeof(names[0])) {
        return "unknown ARM64 block cache slot state";
    }

    return names[(size_t)state];
}
