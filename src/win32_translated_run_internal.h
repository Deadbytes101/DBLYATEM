#ifndef DBLYAT_WIN32_TRANSLATED_RUN_INTERNAL_H
#define DBLYAT_WIN32_TRANSLATED_RUN_INTERNAL_H

#include "dblyat/win32_translated_run.h"

#include <stdint.h>

typedef db_win32_translated_execution_status
 (*db_win32_translated_run_executor)(
    void *context,
    const db_arm64_published_block *published,
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_execution_summary *summary);

db_win32_translated_run_status db_win32_translated_run_with_executor(
    const db_win32_translated_run_config *config,
    db_win32_kernel32_state *kernel32_state,
    db_x64_scalar_state *scalar_state,
    db_win32_translated_run_summary *summary,
    db_win32_translated_run_executor executor,
    void *executor_context);

#endif
