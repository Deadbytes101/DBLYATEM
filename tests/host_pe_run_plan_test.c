#include "dblyat/host_pe_run.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        (void)printf("HOST PE RUN PLAN CHECK: FAIL line=%d\n", __LINE__); \
        return 1; \
    } \
} while (0)

int main(void)
{
    db_pe_image pe;
    db_host_pe_run_layout plan;
    (void)memset(&pe, 0, sizeof(pe));
    (void)memset(&plan, 0, sizeof(plan));

    CHECK(db_host_pe_run_plan(NULL, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_NULL_ARGUMENT);
    CHECK(db_host_pe_run_plan(&pe, 4096u, NULL) ==
          DB_HOST_PE_RUN_PLAN_NULL_ARGUMENT);
    pe.image_base = UINT64_C(0x140000000);
    pe.image_size = 0x3001u;
    CHECK(db_host_pe_run_plan(&pe, 0u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_PAGE_SIZE);
    CHECK(db_host_pe_run_plan(&pe, 8193u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_PAGE_SIZE);
    CHECK(db_host_pe_run_plan(&pe, 2048u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_PAGE_SIZE);
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_OK);
    CHECK(plan.image_size == 0x3001u);
    CHECK(plan.image_page_count == 4u);
    CHECK(plan.image_page_size == 4096u);
    CHECK(plan.loaded_base == pe.image_base);
    CHECK(db_host_pe_run_plan(&pe, 16384u, &plan) ==
          DB_HOST_PE_RUN_PLAN_OK);
    CHECK(plan.image_page_count == 1u);
    (void)puts("HOST PE RUN PLAN PAGE BOUNDS: PASS");

    pe.image_size = 0u;
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_IMAGE_SIZE);
    pe.image_size = DB_HOST_PE_RUN_MAX_IMAGE + 1u;
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_IMAGE_SIZE);
    pe.image_size = 0x2000u;
    pe.image_base = 0u;
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_GUEST_RANGE);
    pe.image_base = UINT64_C(0x7ffffffff000);
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_GUEST_RANGE);
    pe.image_base = DB_HOST_PE_RUN_GATEWAY_BASE;
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_GUEST_RANGE);
    pe.image_base = DB_HOST_PE_RUN_STACK_BASE - UINT64_C(4096);
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_BAD_GUEST_RANGE);
    pe.image_base = UINT64_C(0x140000000);
    CHECK(db_host_pe_run_plan(&pe, 4096u, &plan) ==
          DB_HOST_PE_RUN_PLAN_OK);
    (void)puts("HOST PE RUN PLAN GUEST RANGES: PASS");

    (void)puts("HOST PE RUN PLAN CHECK: PASS");
    return 0;
}
