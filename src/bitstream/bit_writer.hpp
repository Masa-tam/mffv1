#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mffv1/result.hpp"

namespace mffv1::bitstream {

class BitWriter {
public:
    void reset() noexcept;

    [[nodiscard]] std::uint64_t bit_position() const noexcept;
    [[nodiscard]] std::uint64_t byte_position() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;

    Status write_bit(std::uint8_t bit);
    Status write_bits(std::uint64_t value, std::uint8_t bit_count);
    Status byte_align_zero();
    Status require_byte_aligned() const noexcept;
    Status finalize(std::vector<std::byte>& out_bytes);

private:
    std::vector<std::byte> bytes_;
    std::uint64_t bit_position_ = 0;
    bool finalized_ = false;
};

} // namespace mffv1::bitstream
