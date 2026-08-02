#include "arm64_translation_reuse_internal.h"

#include <string.h>

const char *db_arm64_translation_reuse_status_name(
    db_arm64_translation_reuse_status status)
{
    switch (status) {
    case DB_ARM64_TRANSLATION_REUSE_OK:
        return "ok";
    case DB_ARM64_TRANSLATION_REUSE_NULL_ARGUMENT:
        return "null argument";
    case DB_ARM64_TRANSLATION_REUSE_BUFFER_OVERLAP:
        return "buffer overlap";
    case DB_ARM64_TRANSLATION_REUSE_BAD_SESSION:
        return "bad session";
    case DB_ARM64_TRANSLATION_REUSE_CACHE_REJECTED:
        return "cache rejected";
    case DB_ARM64_TRANSLATION_REUSE_COUNTER_OVERFLOW:
        return "counter overflow";
    case DB_ARM64_TRANSLATION_REUSE_CACHE_INCONSISTENT:
        return "cache inconsistent";
    }

    return "unknown";
}
const char *db_arm64_translation_reuse_operation_kind_name(
    db_arm64_translation_reuse_operation_kind kind)
{
    switch (kind) {
    case DB_ARM64_TRANSLATION_REUSE_OPERATION_LOOKUP:
        return "lookup";
    case DB_ARM64_TRANSLATION_REUSE_OPERATION_PUBLICATION:
        return "publication";
    }

    return "unknown";
}
