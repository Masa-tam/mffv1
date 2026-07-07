function(mffv1_package_smoke_run)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE mffv1_package_smoke_result
        COMMAND_ECHO STDOUT
    )
    if(NOT mffv1_package_smoke_result EQUAL 0)
        message(FATAL_ERROR
            "package smoke command failed with exit code "
            "${mffv1_package_smoke_result}"
        )
    endif()
endfunction()

get_filename_component(
    MFFV1_PACKAGE_SMOKE_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/.."
    ABSOLUTE
)

if(NOT DEFINED MFFV1_PACKAGE_SMOKE_PROJECT_BUILD_DIR)
    message(FATAL_ERROR
        "Set MFFV1_PACKAGE_SMOKE_PROJECT_BUILD_DIR to the configured mffv1 build directory"
    )
endif()

if(NOT DEFINED MFFV1_PACKAGE_SMOKE_INSTALL_DIR)
    set(MFFV1_PACKAGE_SMOKE_INSTALL_DIR
        "${MFFV1_PACKAGE_SMOKE_SOURCE_DIR}/build/package-smoke/install"
    )
endif()

if(NOT DEFINED MFFV1_PACKAGE_SMOKE_BUILD_DIR)
    set(MFFV1_PACKAGE_SMOKE_BUILD_DIR
        "${MFFV1_PACKAGE_SMOKE_SOURCE_DIR}/build/package-smoke/build"
    )
endif()

get_filename_component(
    MFFV1_PACKAGE_SMOKE_PROJECT_BUILD_DIR
    "${MFFV1_PACKAGE_SMOKE_PROJECT_BUILD_DIR}"
    ABSOLUTE
)
get_filename_component(
    MFFV1_PACKAGE_SMOKE_INSTALL_DIR
    "${MFFV1_PACKAGE_SMOKE_INSTALL_DIR}"
    ABSOLUTE
)
get_filename_component(
    MFFV1_PACKAGE_SMOKE_BUILD_DIR
    "${MFFV1_PACKAGE_SMOKE_BUILD_DIR}"
    ABSOLUTE
)

if(NOT DEFINED MFFV1_PACKAGE_SMOKE_CONFIG)
    set(MFFV1_PACKAGE_SMOKE_CONFIG Debug)
endif()

if(NOT DEFINED MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES)
    set(MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES 1)
endif()

set(mffv1_package_smoke_configure_command
    "${CMAKE_COMMAND}"
    -S "${MFFV1_PACKAGE_SMOKE_SOURCE_DIR}/tests/package_smoke"
    -B "${MFFV1_PACKAGE_SMOKE_BUILD_DIR}"
    "-DCMAKE_PREFIX_PATH=${MFFV1_PACKAGE_SMOKE_INSTALL_DIR}"
    "-DMFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES=${MFFV1_PACKAGE_SMOKE_EXPECT_STATUS_MESSAGES}"
)

if(DEFINED MFFV1_PACKAGE_SMOKE_GENERATOR)
    list(APPEND mffv1_package_smoke_configure_command
        -G "${MFFV1_PACKAGE_SMOKE_GENERATOR}"
    )
endif()

if(DEFINED MFFV1_PACKAGE_SMOKE_ARCHITECTURE)
    list(APPEND mffv1_package_smoke_configure_command
        -A "${MFFV1_PACKAGE_SMOKE_ARCHITECTURE}"
    )
endif()

mffv1_package_smoke_run(
    "${CMAKE_COMMAND}"
    --install "${MFFV1_PACKAGE_SMOKE_PROJECT_BUILD_DIR}"
    --config "${MFFV1_PACKAGE_SMOKE_CONFIG}"
    --prefix "${MFFV1_PACKAGE_SMOKE_INSTALL_DIR}"
)

mffv1_package_smoke_run(${mffv1_package_smoke_configure_command})

mffv1_package_smoke_run(
    "${CMAKE_COMMAND}"
    --build "${MFFV1_PACKAGE_SMOKE_BUILD_DIR}"
    --config "${MFFV1_PACKAGE_SMOKE_CONFIG}"
)

set(mffv1_package_smoke_executable_name mffv1_package_smoke)
if(WIN32)
    string(APPEND mffv1_package_smoke_executable_name ".exe")
endif()

set(mffv1_package_smoke_executable
    "${MFFV1_PACKAGE_SMOKE_BUILD_DIR}/${MFFV1_PACKAGE_SMOKE_CONFIG}/${mffv1_package_smoke_executable_name}"
)
if(NOT EXISTS "${mffv1_package_smoke_executable}")
    set(mffv1_package_smoke_executable
        "${MFFV1_PACKAGE_SMOKE_BUILD_DIR}/${mffv1_package_smoke_executable_name}"
    )
endif()

if(NOT EXISTS "${mffv1_package_smoke_executable}")
    message(FATAL_ERROR
        "package smoke executable was not found: ${mffv1_package_smoke_executable}"
    )
endif()

mffv1_package_smoke_run("${mffv1_package_smoke_executable}")
