cmake_minimum_required(VERSION 3.22.1)

project(dblyatem_pr98_android_evidence C)

if(NOT DEFINED DBLYATEM_FEATURE_DIR)
    message(FATAL_ERROR "DBLYATEM_FEATURE_DIR is required")
endif()

get_filename_component(
    DBLYATEM_FEATURE_DIR
    "${DBLYATEM_FEATURE_DIR}"
    ABSOLUTE
)

set(DBLYATEM_EXPECTED_HEAD
    "8886fe231eaaefd9c28ed6fe9a7dc404739cc566"
)

set(DBLYATEM_REQUIRED_FILES
    "${DBLYATEM_FEATURE_DIR}/include/dblyat/linux_arm64_wx_provider.h"
    "${DBLYATEM_FEATURE_DIR}/include/dblyat/arm64_code_image.h"
    "${DBLYATEM_FEATURE_DIR}/src/linux_arm64_wx_provider.c"
    "${DBLYATEM_FEATURE_DIR}/src/linux_arm64_wx_posix.c"
    "${DBLYATEM_FEATURE_DIR}/tests/linux_arm64_wx_provider_test.c"
    "${DBLYATEM_FEATURE_DIR}/tests/linux_arm64_wx_posix_test.c"
    "${DBLYATEM_FEATURE_DIR}/tests/linux_arm64_wx_execute_test.c"
)

foreach(required_file IN LISTS DBLYATEM_REQUIRED_FILES)
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing exact-head source: ${required_file}")
    endif()
endforeach()

set(DBLYATEM_STRICT_OPTIONS
    -O2
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wstrict-prototypes
    -Wmissing-prototypes
)

function(dblyatem_test_object target source renamed_main)
    add_library(${target} OBJECT "${source}")
    target_include_directories(
        ${target}
        PRIVATE
        "${DBLYATEM_FEATURE_DIR}/include"
    )
    target_compile_definitions(
        ${target}
        PRIVATE
        "main=${renamed_main}"
    )
    target_compile_options(
        ${target}
        PRIVATE
        ${DBLYATEM_STRICT_OPTIONS}
    )
    set_property(
        TARGET ${target}
        PROPERTY POSITION_INDEPENDENT_CODE ON
    )
endfunction()

dblyatem_test_object(
    dblyatem_provider_test_object
    "${DBLYATEM_FEATURE_DIR}/tests/linux_arm64_wx_provider_test.c"
    dblyatem_provider_test_main
)

dblyatem_test_object(
    dblyatem_posix_test_object
    "${DBLYATEM_FEATURE_DIR}/tests/linux_arm64_wx_posix_test.c"
    dblyatem_posix_test_main
)

dblyatem_test_object(
    dblyatem_execute_test_object
    "${DBLYATEM_FEATURE_DIR}/tests/linux_arm64_wx_execute_test.c"
    dblyatem_execute_test_main
)

add_library(
    pr98evidence
    SHARED
    native_bridge.c
    "${DBLYATEM_FEATURE_DIR}/src/linux_arm64_wx_provider.c"
    "${DBLYATEM_FEATURE_DIR}/src/linux_arm64_wx_posix.c"
    $<TARGET_OBJECTS:dblyatem_provider_test_object>
    $<TARGET_OBJECTS:dblyatem_posix_test_object>
    $<TARGET_OBJECTS:dblyatem_execute_test_object>
)

target_include_directories(
    pr98evidence
    PRIVATE
    "${DBLYATEM_FEATURE_DIR}/include"
)

target_compile_definitions(
    pr98evidence
    PRIVATE
    "DBLYATEM_EXPECTED_HEAD=\"${DBLYATEM_EXPECTED_HEAD}\""
)

target_compile_options(
    pr98evidence
    PRIVATE
    ${DBLYATEM_STRICT_OPTIONS}
)

target_link_libraries(
    pr98evidence
    PRIVATE
    log
)
