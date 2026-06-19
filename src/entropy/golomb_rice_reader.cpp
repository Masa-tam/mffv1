#include "entropy/golomb_rice_reader.hpp"

#include "util/status.hpp"

#include <cstdint>
#include <limits>
#include <utility>

namespace mffv1::entropy {

namespace {

Status add_start_offset(Status status, std::uint64_t start_bit) noexcept
{
    set_byte_location_if_missing(status, start_bit / 8);
    return status;
}

} // namespace

GolombRiceReader::GolombRiceReader(bitstream::BitReader& reader) noexcept
    : reader_(reader)
{
}

Status GolombRiceReader::read_signed(std::uint8_t k,
                                     std::uint8_t bits_per_raw_sample,
                                     std::int32_t& out_value) noexcept
{
    const auto start_bit = reader_.bit_position();
    if (k >= 32) {
        return make_byte_error(ErrorCode::InvalidArgument,
                               "Golomb-Rice parameter k must be less than 32",
                               start_bit / 8);
    }
    if (bits_per_raw_sample == 0 || bits_per_raw_sample > 31) {
        return make_byte_error(ErrorCode::InvalidArgument,
                               "Golomb-Rice raw sample width must be in the range 1..31",
                               start_bit / 8);
    }

    std::uint64_t folded = 0;
    Status status = read_unsigned(k, bits_per_raw_sample, folded);
    if (!status.ok()) {
        return add_start_offset(std::move(status), start_bit);
    }

    const std::int64_t magnitude = static_cast<std::int64_t>(folded >> 1);
    const std::int64_t value = (folded & 1u) != 0 ? -magnitude - 1 : magnitude;
    if (value < std::numeric_limits<std::int32_t>::min()
        || value > std::numeric_limits<std::int32_t>::max()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "Golomb-Rice signed value is outside the supported range",
                               start_bit / 8);
    }
    out_value = static_cast<std::int32_t>(value);
    return ok_status();
}

Status GolombRiceReader::read_unsigned(std::uint8_t k,
                                       std::uint8_t bits_per_raw_sample,
                                       std::uint64_t& out_value) noexcept
{
    std::uint8_t prefix = 0;
    for (; prefix < 12; ++prefix) {
        std::uint8_t bit = 0;
        Status status = reader_.read_bit(bit);
        if (!status.ok()) {
            return status;
        }
        if (bit != 0) {
            std::uint64_t suffix = 0;
            status = reader_.read_bits(k, suffix);
            if (!status.ok()) {
                return status;
            }
            out_value = (static_cast<std::uint64_t>(prefix) << k) + suffix;
            return ok_status();
        }
    }

    std::uint64_t escaped = 0;
    Status status = reader_.read_bits(bits_per_raw_sample, escaped);
    if (!status.ok()) {
        return status;
    }
    out_value = escaped + 11;
    const auto maximum_regular = (std::uint64_t{12} << k) - 1;
    if (out_value <= maximum_regular) {
        return make_error(ErrorCode::SyntaxError,
                          "Golomb-Rice escape encodes a value representable without escape");
    }
    return ok_status();
}

} // namespace mffv1::entropy
