#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#if __has_include("mffv1/build_config.hpp")
#include "mffv1/build_config.hpp"
#endif

#ifndef MFFV1_ENABLE_STATUS_MESSAGES
#define MFFV1_ENABLE_STATUS_MESSAGES 1
#endif

namespace mffv1 {

enum class ErrorCode : std::uint32_t {
    Ok = 0,
    InvalidArgument,
    InvalidState,
    UnsupportedFeature,
    SyntaxError,
    CrcMismatch,
    ResourceExhausted,
    NotImplemented,
    InternalError,
};

struct ErrorLocation {
    std::uint64_t byte_offset = 0;
    std::uint32_t frame_index = 0;
    std::uint32_t slice_index = 0;
    bool has_byte_offset = false;
    bool has_frame_index = false;
    bool has_slice_index = false;
};

struct Status {
    ErrorCode code = ErrorCode::Ok;
    std::string message;
    ErrorLocation location;

    [[nodiscard]] bool ok() const noexcept;
};

Status ok_status();
Status make_error(ErrorCode code, std::string message);

} // namespace mffv1
