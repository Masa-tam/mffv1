#include "bitstream/bit_writer.hpp"

#include <limits>
#include <utility>

namespace mffv1::bitstream {

void BitWriter::reset() noexcept
{
    bytes_.clear();
    bit_position_ = 0;
    finalized_ = false;
}

std::uint64_t BitWriter::bit_position() const noexcept
{
    return bit_position_;
}

std::uint64_t BitWriter::byte_position() const noexcept
{
    return bit_position_ / 8;
}

bool BitWriter::finalized() const noexcept
{
    return finalized_;
}

Status BitWriter::write_bit(std::uint8_t bit)
{
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "cannot write to a finalized bitstream");
    }
    if (bit > 1) {
        return make_error(ErrorCode::InvalidArgument, "bit value must be zero or one");
    }
    if (bit_position_ == std::numeric_limits<std::uint64_t>::max()) {
        return make_error(ErrorCode::ResourceExhausted, "bitstream position exceeds uint64_t");
    }

    const auto bit_in_byte = static_cast<unsigned>(bit_position_ % 8);
    if (bit_in_byte == 0) {
        if (bytes_.size() == bytes_.max_size()) {
            return make_error(ErrorCode::ResourceExhausted, "bitstream exceeds vector capacity");
        }
        bytes_.push_back(std::byte{0});
    }
    if (bit != 0) {
        bytes_.back() |= static_cast<std::byte>(std::uint8_t{1} << (7u - bit_in_byte));
    }
    ++bit_position_;
    return ok_status();
}

Status BitWriter::write_bits(std::uint64_t value, std::uint8_t bit_count)
{
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "cannot write to a finalized bitstream");
    }
    if (bit_count > 64) {
        return make_error(ErrorCode::InvalidArgument, "cannot write more than 64 bits at once");
    }
    if (bit_count < 64 && value >= (std::uint64_t{1} << bit_count)) {
        return make_error(ErrorCode::InvalidArgument, "value does not fit in the requested bit count");
    }
    if (bit_position_ > std::numeric_limits<std::uint64_t>::max() - bit_count) {
        return make_error(ErrorCode::ResourceExhausted, "bitstream position exceeds uint64_t");
    }

    for (std::uint8_t i = 0; i < bit_count; ++i) {
        const auto shift = static_cast<unsigned>(bit_count - 1 - i);
        const auto bit = static_cast<std::uint8_t>((value >> shift) & 1u);
        const Status status = write_bit(bit);
        if (!status.ok()) {
            return status;
        }
    }
    return ok_status();
}

Status BitWriter::byte_align_zero()
{
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "cannot align a finalized bitstream");
    }
    const auto remainder = static_cast<std::uint8_t>(bit_position_ % 8);
    if (remainder == 0) {
        return ok_status();
    }
    return write_bits(0, static_cast<std::uint8_t>(8 - remainder));
}

Status BitWriter::require_byte_aligned() const noexcept
{
    if ((bit_position_ % 8) != 0) {
        return make_error(ErrorCode::InvalidState, "bitstream is not byte aligned");
    }
    return ok_status();
}

Status BitWriter::finalize(std::vector<std::byte>& out_bytes)
{
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "bitstream is already finalized");
    }
    Status status = require_byte_aligned();
    if (!status.ok()) {
        return status;
    }

    auto completed = bytes_;
    out_bytes = std::move(completed);
    finalized_ = true;
    return ok_status();
}

} // namespace mffv1::bitstream
