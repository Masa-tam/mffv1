#pragma once

#include <cstdint>

namespace ffv1::syntax {

class Predictor {
public:
    static std::int32_t median_predict(std::int32_t left,
                                       std::int32_t top,
                                       std::int32_t top_left) noexcept;

    static std::int32_t reconstruct(std::int32_t prediction,
                                    std::int32_t difference,
                                    std::uint8_t bits_per_raw_sample) noexcept;
};

} // namespace ffv1::syntax

