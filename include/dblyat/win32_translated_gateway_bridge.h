#ifndef DBLYAT_WIN32_TRANSLATED_GATEWAY_BRIDGE_H
#define DBLYAT_WIN32_TRANSLATED_GATEWAY_BRIDGE_H

#include "dblyat/gateway_decode.h"
#include "dblyat/translated_terminal_exit.h"
#include "dblyat/win32_runtime_dispatch.h"
#include "dblyat/x64_interpret.h"

#include <stddef.h>
#include <stdint.h>

#define DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_FORMAT_VERSION UINT32_C(1)

typedef enum db_win32_translated_gateway_bridge_status {
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_OK = 0,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NULL_ARGUMENT,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_COUNT_OVERFLOW,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BUFFER_OVERLAP,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BAD_TERMINAL_EXIT,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_BAD_SCALAR_STATE,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_LOOKUP_FAILED,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_NOT_GATEWAY,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_GATEWAY_BUFFER_TOO_SMALL,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_AUTHENTICATION_FAILED,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_IDENTITY_MISMATCH,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_ENTRY_FAILED,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_INVOKE_FAILED,
    DB_WIN32_TRANSLATED_GATEWAY_BRIDGE_RUNTIME_DISPATCH_FAILED
} db_win32_translated_gateway_bridge_status;

typedef struct db_win32_translated_gateway_bridge_summary {
    db_gateway_decode_summary decode;
    db_gateway_enter_summary entry;
    db_gateway_invoke_request request;
    db_win32_runtime_dispatch_summary dispatch;
    db_iat_target authenticated_target;
    size_t slot_index;
    uint32_t format_version;
    uint8_t gateway_consumed;
    uint8_t reserved[3];
} db_win32_translated_gateway_bridge_summary;

db_win32_translated_gateway_bridge_status
 db_win32_translated_gateway_bridge_run(
    const db_translated_terminal_exit *terminal_exit,
    const void *gateway_buffer,
    size_t gateway_buffer_size,
    const db_iat_target *targets,
    size_t target_count,
    uint64_t gateway_base,
    uint64_t slot_size,
    const db_gateway_dispatch_handler *handlers,
    size_t handler_count,
    const db_gateway_frame_signature *signatures,
    size_t signature_count,
    const void *guest_stack,
    size_t guest_stack_size,
    uint64_t guest_stack_base,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_gateway_bridge_summary *summary);

const char *db_win32_translated_gateway_bridge_status_name(
    db_win32_translated_gateway_bridge_status status);

#endif
