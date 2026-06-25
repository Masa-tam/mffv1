#pragma once

#include <cstdint>

#include "bitstream/bit_writer.hpp"

namespace mffv1::entropy {

class GolombRiceWriter final {
public:
    explicit GolombRiceWriter(bitstream::BitWriter& writer) noexcept;

    Status write_signed(std::uint8_t k,
                        std::uint8_t bits_per_raw_sample,
                        std::int32_t value);

private:
    Status write_unsigned(std::uint8_t k,
                          std::uint8_t bits_per_raw_sample,
                          std::uint64_t value);

    bitstream::BitWriter& writer_;
};

} // namespace mffv1::entropy
