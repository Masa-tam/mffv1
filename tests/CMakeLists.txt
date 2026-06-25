find_package(GTest CONFIG QUIET)

if(NOT GTest_FOUND)
    if(EXISTS "${PROJECT_SOURCE_DIR}/third-party/googletest/CMakeLists.txt")
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        add_subdirectory(
            "${PROJECT_SOURCE_DIR}/third-party/googletest"
            "${PROJECT_BINARY_DIR}/third-party/googletest"
            EXCLUDE_FROM_ALL
        )
    else()
        message(FATAL_ERROR
            "MFFV1_BUILD_TESTS is ON, but GoogleTest was not found. "
            "Install GoogleTest, initialize third-party/googletest, "
            "or configure with -DMFFV1_BUILD_TESTS=OFF."
        )
    endif()
endif()

add_executable(mffv1_unit_tests
    unit/bit_reader_tests.cpp
    unit/bit_writer_tests.cpp
    unit/color_transform_tests.cpp
    unit/context_model_tests.cpp
    unit/crc32_tests.cpp
    unit/decoder_tests.cpp
    unit/encoder_tests.cpp
    unit/frame_parser_tests.cpp
    unit/configuration_parser_tests.cpp
    unit/configuration_record_writer_tests.cpp
    unit/frame_validator_tests.cpp
    unit/golomb_rice_context_tests.cpp
    unit/golomb_rice_reader_tests.cpp
    unit/golomb_rice_run_tests.cpp
    unit/predictor_tests.cpp
    unit/range_coder_tests.cpp
    unit/range_encoder_tests.cpp
    unit/rfc_math_tests.cpp
    unit/multi_slice_decode_tests.cpp
    unit/slice_decoder_tests.cpp
    unit/slice_encoder_tests.cpp
    unit/slice_executor_tests.cpp
    unit/slice_footer_parser_tests.cpp
    unit/slice_footer_writer_tests.cpp
    unit/slice_header_parser_tests.cpp
    unit/slice_header_writer_tests.cpp
    unit/slice_output_window_tests.cpp
    unit/slice_payload_locator_tests.cpp
    unit/slice_raster_validator_tests.cpp
    unit/stream_parameters_tests.cpp
)

target_link_libraries(mffv1_unit_tests
    PRIVATE
        mffv1::mffv1
        GTest::gtest_main
)

target_include_directories(mffv1_unit_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)

mffv1_apply_common_options(mffv1_unit_tests)

include(GoogleTest)
gtest_discover_tests(mffv1_unit_tests)
