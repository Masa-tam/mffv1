#include "bitstream/bit_reader.hpp"

#include <limits>

namespace ffv1::bitstream {

BitReader::BitReader(std::span<const std::byte> data) noexcept
    : data_(data)
{
}

std::uint64_t BitReader::bit_position() const noexcept
{
    return bit_position_;
}

std::uint64_t BitReader::byte_position() const noexcept
{
    return bit_position_ / 8;
}

std::uint64_t BitReader::remaining_bits() const noexcept
{
    const auto total_bits = static_cast<std::uint64_t>(data_.size()) * 8;
    return bit_position_ <= total_bits ? total_bits - bit_position_ : 0;
}

Status BitReader::read_bit(std::uint8_t& out_bit) noexcept
{
    if (remaining_bits() == 0) {
        return make_error(ErrorCode::SyntaxError, "bitstream underflow while reading one bit");
    }

    const auto byte_index = static_cast<std::size_t>(bit_position_ / 8);
    const auto bit_index = static_cast<unsigned>(7 - (bit_position_ % 8));
    const auto value = static_cast<std::uint8_t>(data_[byte_index]);
    out_bit = static_cast<std::uint8_t>((value >> bit_index) & 1u);
    ++bit_position_;
    return ok_status();
}

Status BitReader::read_bits(std::uint8_t bit_count, std::uint64_t& out_value) noexcept
{
    if (bit_count > 64) {
        return make_error(ErrorCode::InvalidArgument, "cannot read more than 64 bits at once");
    }
    if (remaining_bits() < bit_count) {
        return make_error(ErrorCode::SyntaxError, "bitstream underflow while reading bits");
    }

    std::uint64_t value = 0;
    for (std::uint8_t i = 0; i < bit_count; ++i) {
        std::uint8_t bit = 0;
        const Status status = read_bit(bit);
        if (!status.ok()) {
            return status;
        }
        value = (value << 1) | bit;
    }

    out_value = value;
    return ok_status();
}

Status BitReader::skip_bits(std::uint64_t bit_count) noexcept
{
    if (remaining_bits() < bit_count) {
        return make_error(ErrorCode::SyntaxError, "bitstream underflow while skipping bits");
    }
    bit_position_ += bit_count;
    return ok_status();
}

Status BitReader::byte_align() noexcept
{
    const auto remainder = bit_position_ % 8;
    if (remainder != 0) {
        bit_position_ += 8 - remainder;
    }
    return ok_status();
}

Status BitReader::require_byte_aligned() const noexcept
{
    if ((bit_position_ % 8) != 0) {
        return make_error(ErrorCode::SyntaxError, "bitstream is not byte aligned");
    }
    return ok_status();
}

} // namespace ffv1::bitstream
