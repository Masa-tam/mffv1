cmake_minimum_required(VERSION 3.25)

project(ffv1_clean
    VERSION 0.1.0
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Ffv1Options)

add_library(ffv1
    src/bitstream/bit_reader.cpp
    src/codec/decoder.cpp
    src/codec/encoder.cpp
    src/codec/frame_parser.cpp
    src/codec/frame_validator.cpp
    src/codec/configuration_record_parser.cpp
    src/codec/slice_decoder.cpp
    src/codec/slice_executor.cpp
    src/codec/slice_footer_parser.cpp
    src/codec/slice_header_parser.cpp
    src/codec/slice_output_window.cpp
    src/codec/slice_payload_locator.cpp
    src/codec/slice_raster_validator.cpp
    src/ffv1/context_model.cpp
    src/entropy/golomb_rice_context.cpp
    src/entropy/golomb_rice_reader.cpp
    src/entropy/golomb_rice_run.cpp
    src/entropy/range_coder.cpp
    src/ffv1/configuration_parser.cpp
    src/ffv1/predictor.cpp
    src/util/crc32.cpp
    src/util/rfc_math.cpp
    src/util/status.cpp
)

add_library(ffv1::ffv1 ALIAS ffv1)

target_compile_features(ffv1 PUBLIC cxx_std_20)

target_include_directories(ffv1
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

ffv1_apply_common_options(ffv1)

include(CTest)
if(BUILD_TESTING AND FFV1_BUILD_TESTS)
    add_subdirectory(tests)
endif()
