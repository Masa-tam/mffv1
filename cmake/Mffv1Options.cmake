option(MFFV1_BUILD_TESTS "Build FFV1 unit and conformance tests" ON)
option(MFFV1_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

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
endfunction()

