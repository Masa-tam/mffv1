#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "mffv1/result.hpp"

namespace mffv1 {

inline void set_byte_location_if_missing(Status& status, std::uint64_t byte_offset) noexcept
{
    if (!status.location.has_byte_offset) {
        status.location.byte_offset = byte_offset;
        status.location.has_byte_offset = true;
    }
}

inline void set_slice_location_if_missing(Status& status, std::uint32_t slice_index) noexcept
{
    if (!status.location.has_slice_index) {
        status.location.slice_index = slice_index;
        status.location.has_slice_index = true;
    }
}

inline Status make_byte_error(ErrorCode code, std::string message, std::uint64_t byte_offset)
{
    Status status = make_error(code, std::move(message));
    set_byte_location_if_missing(status, byte_offset);
    return status;
}

} // namespace mffv1
