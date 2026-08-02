#ifndef DBLYAT_ARM64_BLOCK_CACHE_H
#define DBLYAT_ARM64_BLOCK_CACHE_H

#include "dblyat/arm64_published_block.h"

#include <stddef.h>
#include <stdint.h>

#define DB_ARM64_BLOCK_CACHE_FORMAT_VERSION UINT32_C(1)

typedef enum db_arm64_block_cache_slot_state {
    DB_ARM64_BLOCK_CACHE_SLOT_EMPTY = 0,
    DB_ARM64_BLOCK_CACHE_SLOT_OCCUPIED
} db_arm64_block_cache_slot_state;

typedef enum db_arm64_block_cache_status {
    DB_ARM64_BLOCK_CACHE_OK = 0,
    DB_ARM64_BLOCK_CACHE_NULL_ARGUMENT,
    DB_ARM64_BLOCK_CACHE_BUFFER_OVERLAP,
    DB_ARM64_BLOCK_CACHE_BAD_CAPACITY,
    DB_ARM64_BLOCK_CACHE_BAD_CACHE,
    DB_ARM64_BLOCK_CACHE_BAD_KEY,
    DB_ARM64_BLOCK_CACHE_BAD_BLOCK,
    DB_ARM64_BLOCK_CACHE_KEY_CONFLICT,
    DB_ARM64_BLOCK_CACHE_FULL,
    DB_ARM64_BLOCK_CACHE_NOT_FOUND
} db_arm64_block_cache_status;

typedef struct db_arm64_block_cache_slot {
    db_arm64_published_block block;
    uint32_t state;
    uint32_t format_version;
    uint8_t reserved[8];
} db_arm64_block_cache_slot;

typedef struct db_arm64_block_cache {
    db_arm64_block_cache_slot *slots;
    size_t capacity;
    size_t count;
    uint32_t format_version;
    uint8_t reserved[12];
} db_arm64_block_cache;

typedef struct db_arm64_block_cache_result {
    uint64_t slot_index;
    uint64_t probe_count;
    uint32_t found;
    uint32_t inserted;
    uint8_t reserved[8];
} db_arm64_block_cache_result;

db_arm64_block_cache_status db_arm64_block_cache_init(
    db_arm64_block_cache *cache,
    db_arm64_block_cache_slot *slots,
    size_t capacity);

db_arm64_block_cache_status db_arm64_block_cache_lookup(
    const db_arm64_block_cache *cache,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key,
    db_arm64_published_block *published,
    db_arm64_block_cache_result *result);

db_arm64_block_cache_status db_arm64_block_cache_insert(
    db_arm64_block_cache *cache,
    const db_arm64_published_block *published,
    db_arm64_block_cache_result *result);

const char *db_arm64_block_cache_status_name(
    db_arm64_block_cache_status status);

const char *db_arm64_block_cache_slot_state_name(
    db_arm64_block_cache_slot_state state);

#endif
