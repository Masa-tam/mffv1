cmake_minimum_required(VERSION 3.25)

project(mffv1
    VERSION 0.1.0
    HOMEPAGE_URL "https://github.com/Masa-tam/mffv1"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
include(Mffv1Options)

set(MFFV1_ENABLE_STATUS_MESSAGES_DEFINE 0)
if(MFFV1_ENABLE_STATUS_MESSAGES)
    set(MFFV1_ENABLE_STATUS_MESSAGES_DEFINE 1)
endif()
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mffv1_build_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include/mffv1/build_config.hpp"
    @ONLY
)

add_library(mffv1 STATIC
    src/bitstream/bit_reader.cpp
    src/bitstream/bit_writer.cpp
    src/codec/decoder.cpp
    src/codec/encoder.cpp
    src/codec/encoder_profile.cpp
    src/codec/frame_info_builder.cpp
    src/codec/frame_parser.cpp
    src/codec/frame_validator.cpp
    src/codec/configuration_record_parser.cpp
    src/codec/configuration_record_writer.cpp
    src/codec/legacy_frame_bootstrap_parser.cpp
    src/codec/slice_decoder.cpp
    src/codec/slice_encode_executor.cpp
    src/codec/slice_encoder.cpp
    src/codec/slice_executor.cpp
    src/codec/slice_footer_parser.cpp
    src/codec/slice_footer_writer.cpp
    src/codec/slice_header_parser.cpp
    src/codec/slice_header_writer.cpp
    src/codec/slice_input_window.cpp
    src/codec/slice_output_window.cpp
    src/codec/slice_payload_locator.cpp
    src/codec/slice_raster_validator.cpp
    src/mffv1/color_transform.cpp
    src/mffv1/context_model.cpp
    src/entropy/golomb_rice_context.cpp
    src/entropy/golomb_rice_reader.cpp
    src/entropy/golomb_rice_run.cpp
    src/entropy/golomb_rice_writer.cpp
    src/entropy/range_coder.cpp
    src/entropy/range_encoder.cpp
    src/mffv1/configuration_parser.cpp
    src/mffv1/predictor.cpp
    src/simd/codec_kernels.cpp
    src/simd/color_transform_avx2.cpp
    src/simd/color_transform_sse2.cpp
    src/simd/cpu_features.cpp
    src/util/crc32.cpp
    src/util/rfc_math.cpp
    src/util/status.cpp
)

add_library(mffv1::mffv1 ALIAS mffv1)

target_compile_features(mffv1 PUBLIC cxx_std_20)
target_include_directories(mffv1
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

mffv1_apply_common_options(mffv1)

if(MSVC AND CMAKE_GENERATOR_PLATFORM MATCHES "^(Win32|x64)$")
    set_source_files_properties(
        src/simd/color_transform_avx2.cpp
        PROPERTIES COMPILE_OPTIONS "/arch:AVX2"
    )
    target_compile_definitions(mffv1 PRIVATE MFFV1_HAS_AVX2_KERNEL=1)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
       AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i.86)$")
    set_source_files_properties(
        src/simd/color_transform_avx2.cpp
        PROPERTIES COMPILE_OPTIONS "-mavx2"
    )
    target_compile_definitions(mffv1 PRIVATE MFFV1_HAS_AVX2_KERNEL=1)
endif()

include(CTest)
if(BUILD_TESTING AND MFFV1_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(MFFV1_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()

install(
    TARGETS mffv1
    EXPORT mffv1Targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

set(MFFV1_PACKAGE_VERSION_COMPATIBILITY SameMajorVersion)
if(PROJECT_VERSION VERSION_LESS 1.0.0)
    set(MFFV1_PACKAGE_VERSION_COMPATIBILITY ExactVersion)
endif()

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/mffv1ConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY "${MFFV1_PACKAGE_VERSION_COMPATIBILITY}"
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mffv1Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mffv1Config.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mffv1"
)

install(
    EXPORT mffv1Targets
    NAMESPACE mffv1::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mffv1"
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/mffv1Config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/mffv1ConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mffv1"
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/CHANGELOG.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/CONTRIBUTING.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/SECURITY.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/docs"
    FILES_MATCHING PATTERN "*.md"
)

install(
    FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/testvectors/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/testvectors/REGISTRY.md"
    DESTINATION "${CMAKE_INSTALL_DOCDIR}/testvectors"
)
