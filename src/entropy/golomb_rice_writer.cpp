#include "entropy/golomb_rice_writer.hpp"

#include <cstdint>

namespace mffv1::entropy {

GolombRiceWriter::GolombRiceWriter(bitstream::BitWriter& writer) noexcept
    : writer_(writer)
{
}

Status GolombRiceWriter::write_signed(std::uint8_t k,
                                      std::uint8_t bits_per_raw_sample,
                                      std::int32_t value)
{
    if (k >= 32) {
        return make_error(
            ErrorCode::InvalidArgument,
            "Golomb-Rice parameter k must be less than 32");
    }
    if (bits_per_raw_sample == 0 || bits_per_raw_sample > 31) {
        return make_error(
            ErrorCode::InvalidArgument,
            "Golomb-Rice raw sample width must be in the range 1..31");
    }

    const std::uint64_t folded = value < 0
        ? static_cast<std::uint64_t>(
            -2 * static_cast<std::int64_t>(value) - 1)
        : static_cast<std::uint64_t>(value) * 2;
    return write_unsigned(k, bits_per_raw_sample, folded);
}

Status GolombRiceWriter::write_unsigned(std::uint8_t k,
                                        std::uint8_t bits_per_raw_sample,
                                        std::uint64_t value)
{
    const auto maximum_regular = (std::uint64_t{12} << k) - 1;
    if (value <= maximum_regular) {
        const auto prefix = value >> k;
        const auto suffix_mask =
            k == 0 ? std::uint64_t{0} : (std::uint64_t{1} << k) - 1;
        for (std::uint64_t index = 0; index < prefix; ++index) {
            Status status = writer_.write_bit(0);
            if (!status.ok()) {
                return status;
            }
        }
        Status status = writer_.write_bit(1);
        if (!status.ok()) {
            return status;
        }
        return writer_.write_bits(value & suffix_mask, k);
    }

    const auto escaped = value - 11;
    const auto maximum_escaped =
        (std::uint64_t{1} << bits_per_raw_sample) - 1;
    if (escaped > maximum_escaped) {
        return make_error(
            ErrorCode::InvalidArgument,
            "Golomb-Rice value does not fit the escape field");
    }
    Status status = writer_.write_bits(0, 12);
    if (!status.ok()) {
        return status;
    }
    return writer_.write_bits(escaped, bits_per_raw_sample);
}

} // namespace mffv1::entropy
