#pragma once

#include <cstdint>

#include "bitstream/bit_reader.hpp"

namespace ffv1::entropy {

class GolombRiceReader final {
public:
    explicit GolombRiceReader(bitstream::BitReader& reader) noexcept;

    Status read_signed(std::uint8_t k,
                       std::uint8_t bits_per_raw_sample,
                       std::int32_t& out_value) noexcept;

private:
    Status read_unsigned(std::uint8_t k,
                         std::uint8_t bits_per_raw_sample,
                         std::uint64_t& out_value) noexcept;

    bitstream::BitReader& reader_;
};

} // namespace ffv1::entropy
