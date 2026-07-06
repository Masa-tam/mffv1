find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    if(EXISTS "${PROJECT_SOURCE_DIR}/third-party/googletest/CMakeLists.txt")
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        add_subdirectory(
            "${PROJECT_SOURCE_DIR}/third-party/googletest"
            "${PROJECT_BINARY_DIR}/third-party/googletest"
            EXCLUDE_FROM_ALL
        )
        foreach(mffv1_gtest_target gtest gtest_main gmock gmock_main)
            if(TARGET ${mffv1_gtest_target})
                mffv1_apply_sanitizer_options(${mffv1_gtest_target})
            endif()
        endforeach()
    else()
        message(FATAL_ERROR
            "MFFV1_BUILD_TESTS is ON, but GoogleTest was not found. "
            "Install GoogleTest, initialize third-party/googletest, "
            "or configure with -DMFFV1_BUILD_TESTS=OFF."
        )
    endif()
endif()

set(mffv1_unit_test_sources
    unit/status_tests.cpp
)

if(NOT MFFV1_BUILD_STATUS_CONTRACT_TESTS_ONLY)
    list(PREPEND mffv1_unit_test_sources
        unit/bit_reader_tests.cpp
        unit/bit_writer_tests.cpp
        unit/color_transform_tests.cpp
        unit/context_model_tests.cpp
        unit/cpu_features_tests.cpp
        unit/crc32_tests.cpp
        unit/decoder_tests.cpp
        unit/encoder_tests.cpp
        unit/encoder_profile_tests.cpp
        unit/frame_info_builder_tests.cpp
        unit/frame_parser_tests.cpp
        unit/configuration_parser_tests.cpp
        unit/configuration_record_parser_tests.cpp
        unit/configuration_record_writer_tests.cpp
        unit/frame_validator_tests.cpp
        unit/golomb_rice_context_tests.cpp
        unit/golomb_rice_reader_tests.cpp
        unit/golomb_rice_run_tests.cpp
        unit/golomb_rice_writer_tests.cpp
        unit/predictor_tests.cpp
        unit/plane_window_math_tests.cpp
        unit/profile_constraints_tests.cpp
        unit/range_coder_tests.cpp
        unit/range_encoder_tests.cpp
        unit/rfc_math_tests.cpp
        unit/sample_format_tests.cpp
        unit/simd_color_transform_tests.cpp
        unit/multi_slice_decode_tests.cpp
        unit/slice_decoder_tests.cpp
        unit/slice_encode_executor_tests.cpp
        unit/slice_encoder_tests.cpp
        unit/slice_executor_tests.cpp
        unit/slice_footer_parser_tests.cpp
        unit/slice_footer_writer_tests.cpp
        unit/slice_header_parser_tests.cpp
        unit/slice_header_writer_tests.cpp
        unit/slice_input_window_tests.cpp
        unit/slice_output_window_tests.cpp
        unit/slice_payload_locator_tests.cpp
        unit/slice_raster_validator_tests.cpp
        unit/stream_parameters_tests.cpp
        unit/test_vector_tests.cpp
        unit/version3_constraints_tests.cpp
    )
endif()

add_executable(mffv1_unit_tests ${mffv1_unit_test_sources})

set(mffv1_test_vector_include_dir "${PROJECT_SOURCE_DIR}/testvectors")
set(mffv1_test_vector_header
    "${mffv1_test_vector_include_dir}/test_vector_data.hpp"
)
if(NOT EXISTS "${mffv1_test_vector_header}")
    set(mffv1_test_vector_include_dir
        "${PROJECT_BINARY_DIR}/generated/testvectors"
    )
    file(MAKE_DIRECTORY "${mffv1_test_vector_include_dir}")
    file(WRITE
        "${mffv1_test_vector_include_dir}/test_vector_data.hpp"
        "#pragma once\n\n#define NO_DEFINE_TEST_VECTOR_DATA 1\n"
    )
endif()

target_link_libraries(mffv1_unit_tests
    PRIVATE
        mffv1::mffv1
        GTest::gtest_main
)

target_include_directories(mffv1_unit_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${mffv1_test_vector_include_dir}"
)

mffv1_apply_common_options(mffv1_unit_tests)

include(GoogleTest)
set(mffv1_test_properties)
if(MSVC AND MFFV1_ENABLE_SANITIZERS)
    get_filename_component(mffv1_msvc_tool_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    list(APPEND mffv1_test_properties
        ENVIRONMENT "PATH=${mffv1_msvc_tool_dir}"
    )
endif()

gtest_discover_tests(mffv1_unit_tests
    PROPERTIES ${mffv1_test_properties}
)
