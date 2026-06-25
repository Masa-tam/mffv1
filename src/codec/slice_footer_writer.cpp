#include "codec/slice_footer_writer.hpp"

#include "codec/slice_footer_parser.hpp"
#include "util/crc32.hpp"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace mffv1::codec {

Status SliceFooterWriter::append(
    const syntax::StreamParameters& stream,
    std::uint8_t error_status,
    std::vector<std::byte>& slice_payload) const
{
    if (!stream.error_status_enabled && error_status != 0) {
        return make_error(
            ErrorCode::InvalidArgument,
            "slice error status requires EC to be enabled");
    }
    if (error_status > 2) {
        return make_error(
            ErrorCode::InvalidArgument,
            "slice error status uses a reserved value");
    }

    const SliceFooterParser parser;
    const auto footer_size = parser.footer_size(stream);
    constexpr std::size_t maximum_slice_size = 0x00ffffffu;
    if (slice_payload.size() > maximum_slice_size - footer_size) {
        return make_error(
            ErrorCode::ResourceExhausted,
            "slice payload exceeds the 24-bit slice size limit");
    }

    auto completed = slice_payload;
    const auto slice_size =
        static_cast<std::uint32_t>(completed.size() + footer_size);
    completed.push_back(
        static_cast<std::byte>((slice_size >> 16) & 0xffu));
    completed.push_back(
        static_cast<std::byte>((slice_size >> 8) & 0xffu));
    completed.push_back(static_cast<std::byte>(slice_size & 0xffu));

    if (stream.error_status_enabled) {
        completed.push_back(static_cast<std::byte>(error_status));
        const auto crc = util::crc32_ieee_msb(completed);
        completed.push_back(static_cast<std::byte>((crc >> 24) & 0xffu));
        completed.push_back(static_cast<std::byte>((crc >> 16) & 0xffu));
        completed.push_back(static_cast<std::byte>((crc >> 8) & 0xffu));
        completed.push_back(static_cast<std::byte>(crc & 0xffu));
    }

    slice_payload = std::move(completed);
    return ok_status();
}

} // namespace mffv1::codec
