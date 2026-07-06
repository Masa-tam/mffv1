option(MFFV1_BUILD_TESTS "Build FFV1 unit and conformance tests" ON)
option(MFFV1_BUILD_STATUS_CONTRACT_TESTS_ONLY "Build only Status contract tests" OFF)
option(MFFV1_BUILD_FUZZERS "Build standalone fuzz harness executables" OFF)
option(MFFV1_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(MFFV1_ENABLE_STATUS_MESSAGES "Store diagnostic text in Status::message" ON)
option(MFFV1_ENABLE_SANITIZERS "Enable compiler sanitizer instrumentation when supported" OFF)

function(mffv1_apply_sanitizer_options target_name)
    if(NOT MFFV1_ENABLE_SANITIZERS)
        return()
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /fsanitize=address)
    else()
        target_compile_options(${target_name} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
        )
        target_link_options(${target_name} PRIVATE
            -fsanitize=address,undefined
        )
    endif()
endfunction()

function(mffv1_apply_common_options target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        target_compile_definitions(${target_name} PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
        )
        if(MFFV1_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(MFFV1_ENABLE_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
    mffv1_apply_sanitizer_options(${target_name})
endfunction()
