cmake_minimum_required(VERSION 3.25)

project(mffv1
    VERSION 0.1.0
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Mffv1Options)

add_library(mffv1
    src/bitstream/bit_reader.cpp
    src/bitstream/bit_writer.cpp
    src/codec/decoder.cpp
    src/codec/encoder.cpp
    src/codec/frame_parser.cpp
    src/codec/frame_validator.cpp
    src/codec/configuration_record_parser.cpp
    src/codec/configuration_record_writer.cpp
    src/codec/slice_decoder.cpp
    src/codec/slice_encoder.cpp
    src/codec/slice_executor.cpp
    src/codec/slice_footer_parser.cpp
    src/codec/slice_footer_writer.cpp
    src/codec/slice_header_parser.cpp
    src/codec/slice_header_writer.cpp
    src/codec/slice_output_window.cpp
    src/codec/slice_payload_locator.cpp
    src/codec/slice_raster_validator.cpp
    src/mffv1/color_transform.cpp
    src/mffv1/context_model.cpp
    src/entropy/golomb_rice_context.cpp
    src/entropy/golomb_rice_reader.cpp
    src/entropy/golomb_rice_run.cpp
    src/entropy/range_coder.cpp
    src/entropy/range_encoder.cpp
    src/mffv1/configuration_parser.cpp
    src/mffv1/predictor.cpp
    src/util/crc32.cpp
    src/util/rfc_math.cpp
    src/util/status.cpp
)

add_library(mffv1::mffv1 ALIAS mffv1)

target_compile_features(mffv1 PUBLIC cxx_std_20)

target_include_directories(mffv1
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

mffv1_apply_common_options(mffv1)

include(CTest)
if(BUILD_TESTING AND MFFV1_BUILD_TESTS)
    add_subdirectory(tests)
endif()
