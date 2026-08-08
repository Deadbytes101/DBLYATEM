#ifndef DBLYAT_WIN32_PE_TRANSLATED_SESSION_INTERNAL_H
#define DBLYAT_WIN32_PE_TRANSLATED_SESSION_INTERNAL_H

#include "dblyat/win32_pe_translated_session.h"
#include "win32_translated_demand_run_internal.h"

#include <stdint.h>

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_prepare_with_helper_targets(
    const db_win32_pe_translated_session_config *config,
    const db_x64_arm64_helper_targets *helper_targets,
    db_win32_pe_translated_session *session);

db_win32_pe_translated_session_status
 db_win32_pe_translated_session_run_with_executor(
    db_win32_pe_translated_session *session,
    db_win32_pe_translated_session_run_summary *summary,
    db_win32_translated_run_executor executor,
    void *executor_context);

#endif
