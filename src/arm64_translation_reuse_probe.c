#include "arm64_translation_reuse_internal.h"

#include <string.h>

uint64_t db_arm64_translation_reuse_internal_mix_u64(uint64_t value)
{
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31u;
    return value;
}
uint64_t db_arm64_translation_reuse_internal_rotate_left(uint64_t value, unsigned int amount)
{
    return (value << amount) | (value >> (64u - amount));
}
size_t db_arm64_translation_reuse_internal_key_home(uint64_t guest_source_rip,
                       uint64_t source_generation,
                       uint64_t translation_key,
                       size_t capacity)
{
    const uint64_t hash =
        db_arm64_translation_reuse_internal_mix_u64(guest_source_rip) ^
        db_arm64_translation_reuse_internal_rotate_left(db_arm64_translation_reuse_internal_mix_u64(source_generation), 21u) ^
        db_arm64_translation_reuse_internal_rotate_left(db_arm64_translation_reuse_internal_mix_u64(translation_key), 42u);

    return (size_t)(hash & (uint64_t)(capacity - 1u));
}
bool db_arm64_translation_reuse_internal_key_equal(const db_arm64_published_block *block,
                      uint64_t guest_source_rip,
                      uint64_t source_generation,
                      uint64_t translation_key)
{
    return block->guest_source_rip == guest_source_rip &&
           block->source_generation == source_generation &&
           block->translation_key == translation_key;
}
db_arm64_translation_reuse_probe_observation db_arm64_translation_reuse_internal_observe_probe(
    const db_arm64_block_cache *cache,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key)
{
    db_arm64_translation_reuse_probe_observation observation;
    const size_t mask = cache->capacity - 1u;
    const size_t home = db_arm64_translation_reuse_internal_key_home(guest_source_rip,
                                 source_generation,
                                 translation_key,
                                 cache->capacity);
    size_t probe;

    observation.slot_index = DB_ARM64_TRANSLATION_REUSE_NO_SLOT;
    observation.probe_count = 0u;
    observation.found = false;

    for (probe = 0u; probe < cache->capacity; ++probe) {
        const size_t index = (home + probe) & mask;
        const db_arm64_block_cache_slot *slot =
            &cache->slots[index];

        observation.probe_count = (uint64_t)(probe + 1u);

        if (slot->state ==
            (uint32_t)DB_ARM64_BLOCK_CACHE_SLOT_EMPTY) {
            observation.slot_index = (uint64_t)index;
            return observation;
        }

        if (db_arm64_translation_reuse_internal_key_equal(&slot->block,
                      guest_source_rip,
                      source_generation,
                      translation_key)) {
            observation.slot_index = (uint64_t)index;
            observation.found = true;
            return observation;
        }
    }

    return observation;
}
bool db_arm64_translation_reuse_internal_cache_result_matches(
    const db_arm64_block_cache_result *result,
    const db_arm64_translation_reuse_probe_observation *observation,
    uint32_t inserted)
{
    return result->slot_index == observation->slot_index &&
           result->probe_count == observation->probe_count &&
           result->found == 1u &&
           result->inserted == inserted &&
           db_arm64_translation_reuse_internal_zero_bytes(result->reserved, sizeof(result->reserved));
}
bool db_arm64_translation_reuse_internal_published_block_valid(
    const db_arm64_published_block *published)
{
    db_arm64_block_cache validation_cache;
    db_arm64_block_cache_slot validation_slot[1];
    db_arm64_block_cache_result validation_result;

    if (db_arm64_block_cache_init(&validation_cache,
                                  validation_slot,
                                  1u) != DB_ARM64_BLOCK_CACHE_OK) {
        return false;
    }

    return db_arm64_block_cache_insert(&validation_cache,
                                       published,
                                       &validation_result) ==
               DB_ARM64_BLOCK_CACHE_OK &&
           validation_result.inserted == 1u;
}
