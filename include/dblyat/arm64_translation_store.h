#ifndef DBLYAT_ARM64_TRANSLATION_STORE_H
#define DBLYAT_ARM64_TRANSLATION_STORE_H

#include "dblyat/arm64_block_cache.h"
#include "dblyat/arm64_code_image.h"
#include "dblyat/arm64_select.h"
#include "dblyat/ir.h"
#include "dblyat/linux_arm64_wx_provider.h"
#include "dblyat/x64_arm64_encode.h"
#include "dblyat/x64_arm64_plan.h"
#include "dblyat/x64_arm64_relocate.h"
#include "dblyat/x64_block.h"
#include "dblyat/x64_ir_lower.h"

#include <stddef.h>
#include <stdint.h>

#define DB_ARM64_TRANSLATION_STORE_FORMAT_VERSION UINT32_C(1)

typedef enum db_arm64_translation_store_state {
    DB_ARM64_TRANSLATION_STORE_EMPTY = 0,
    DB_ARM64_TRANSLATION_STORE_READY,
    DB_ARM64_TRANSLATION_STORE_POISONED
} db_arm64_translation_store_state;

typedef enum db_arm64_translation_store_outcome {
    DB_ARM64_TRANSLATION_STORE_OUTCOME_NONE = 0,
    DB_ARM64_TRANSLATION_STORE_OUTCOME_HIT,
    DB_ARM64_TRANSLATION_STORE_OUTCOME_PUBLISHED
} db_arm64_translation_store_outcome;

typedef enum db_arm64_translation_store_status {
    DB_ARM64_TRANSLATION_STORE_OK = 0,
    DB_ARM64_TRANSLATION_STORE_NULL_ARGUMENT,
    DB_ARM64_TRANSLATION_STORE_BUFFER_OVERLAP,
    DB_ARM64_TRANSLATION_STORE_BAD_CAPACITY,
    DB_ARM64_TRANSLATION_STORE_BAD_STORE,
    DB_ARM64_TRANSLATION_STORE_BAD_WORKSPACE,
    DB_ARM64_TRANSLATION_STORE_BAD_CONFIGURATION,
    DB_ARM64_TRANSLATION_STORE_CACHE_FAILED,
    DB_ARM64_TRANSLATION_STORE_CODE_BASE_FAILED,
    DB_ARM64_TRANSLATION_STORE_BLOCK_FAILED,
    DB_ARM64_TRANSLATION_STORE_BLOCK_NOT_TERMINATED,
    DB_ARM64_TRANSLATION_STORE_IR_FAILED,
    DB_ARM64_TRANSLATION_STORE_SELECT_FAILED,
    DB_ARM64_TRANSLATION_STORE_PLAN_FAILED,
    DB_ARM64_TRANSLATION_STORE_ENCODE_FAILED,
    DB_ARM64_TRANSLATION_STORE_RELOCATE_FAILED,
    DB_ARM64_TRANSLATION_STORE_IMAGE_FAILED,
    DB_ARM64_TRANSLATION_STORE_WX_FAILED,
    DB_ARM64_TRANSLATION_STORE_PUBLISHED_BLOCK_FAILED,
    DB_ARM64_TRANSLATION_STORE_TERMINAL_BIND_FAILED,
    DB_ARM64_TRANSLATION_STORE_INSERT_FAILED,
    DB_ARM64_TRANSLATION_STORE_ROLLBACK_FAILED,
    DB_ARM64_TRANSLATION_STORE_RELEASE_FAILED
} db_arm64_translation_store_status;

typedef int (*db_arm64_translation_code_base_fn)(
    void *context,
    uint64_t guest_source_rip,
    uint64_t source_generation,
    uint64_t translation_key,
    uint64_t page_size,
    uint64_t *code_base);

typedef struct db_arm64_translation_workspace {
    db_x64_block_record *block_records;
    db_ir_instruction *ir;
    db_arm64_select_record *selections;
    db_x64_arm64_plan_record *plans;
    uint32_t *source_words;
    db_x64_arm64_relocation *relocations;
    uint32_t *output_words;
    db_x64_arm64_resolution *resolutions;
    uint8_t *image_bytes;
    uint32_t block_record_capacity;
    uint32_t ir_capacity;
    uint32_t selection_capacity;
    uint32_t plan_capacity;
    uint32_t source_word_capacity;
    uint32_t relocation_capacity;
    uint32_t output_word_capacity;
    uint32_t resolution_capacity;
    size_t image_capacity;
    uint8_t reserved[8];
} db_arm64_translation_workspace;

typedef struct db_arm64_translation_store_config {
    const db_x64_code_view *code;
    const db_x64_arm64_helper_targets *helper_targets;
    const db_linux_arm64_wx_platform *wx_platform;
    void *code_base_context;
    db_arm64_translation_code_base_fn reserve_code_base;
    uint64_t source_generation;
    uint64_t translation_key;
    uint64_t page_size;
    uint32_t instruction_budget;
    uint32_t byte_budget;
    uint8_t reserved[8];
} db_arm64_translation_store_config;

typedef struct db_arm64_translation_store {
    db_arm64_block_cache cache;
    db_linux_arm64_wx_handle *handles;
    uint8_t *owned;
    size_t capacity;
    db_linux_arm64_wx_handle rollback_handle;
    uint32_t state;
    uint32_t format_version;
    uint8_t reserved[8];
} db_arm64_translation_store;

typedef struct db_arm64_translation_store_summary {
    db_arm64_block_cache_result cache_result;
    db_x64_block_summary block;
    db_x64_ir_lower_summary ir;
    db_arm64_select_summary select;
    db_x64_arm64_plan_summary plan;
    db_x64_arm64_encode_summary encode;
    db_x64_arm64_relocate_summary relocate;
    db_arm64_code_image_plan image;
    db_linux_arm64_wx_audit wx;
    uint64_t requested_code_base;
    uint32_t outcome;
    uint32_t cache_status;
    uint32_t block_status;
    uint32_t ir_status;
    uint32_t select_status;
    uint32_t plan_status;
    uint32_t encode_status;
    uint32_t relocate_status;
    uint32_t image_status;
    uint32_t wx_status;
    uint32_t published_status;
    uint32_t format_version;
    uint8_t reserved[4];
} db_arm64_translation_store_summary;

db_arm64_translation_store_status db_arm64_translation_store_init(
    db_arm64_translation_store *store,
    db_arm64_block_cache_slot *slots,
    db_linux_arm64_wx_handle *handles,
    uint8_t *owned,
    size_t capacity);

db_arm64_translation_store_status db_arm64_translation_store_fill(
    db_arm64_translation_store *store,
    const db_arm64_translation_store_config *config,
    db_arm64_translation_workspace *workspace,
    uint64_t guest_source_rip,
    db_arm64_published_block *published,
    db_arm64_translation_store_summary *summary);

db_arm64_translation_store_status db_arm64_translation_store_dispose(
    db_arm64_translation_store *store,
    const db_linux_arm64_wx_platform *platform);

const char *db_arm64_translation_store_status_name(
    db_arm64_translation_store_status status);
const char *db_arm64_translation_store_state_name(
    db_arm64_translation_store_state state);
const char *db_arm64_translation_store_outcome_name(
    db_arm64_translation_store_outcome outcome);

#endif
