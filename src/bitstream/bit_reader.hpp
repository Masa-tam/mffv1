#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "mffv1/result.hpp"

namespace mffv1::bitstream {

class BitReader {
public:
    explicit BitReader(std::span<const std::byte> data) noexcept;

    [[nodiscard]] std::uint64_t bit_position() const noexcept;
    [[nodiscard]] std::uint64_t byte_position() const noexcept;
    [[nodiscard]] std::uint64_t remaining_bits() const noexcept;

    Status read_bit(std::uint8_t& out_bit) noexcept;
    Status read_bits(std::uint8_t bit_count, std::uint64_t& out_value) noexcept;
    Status skip_bits(std::uint64_t bit_count) noexcept;
    Status byte_align() noexcept;
    Status require_byte_aligned() const noexcept;

private:
    std::span<const std::byte> data_;
    std::uint64_t bit_position_ = 0;
};

} // namespace mffv1::bitstream
