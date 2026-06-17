#include "ffv1/predictor.hpp"

#include "util/rfc_math.hpp"

namespace ffv1::syntax {

std::int32_t Predictor::median_predict(std::int32_t left,
                                       std::int32_t top,
                                       std::int32_t top_left) noexcept
{
    return util::median3(left, top, left + top - top_left);
}

std::int32_t Predictor::reconstruct(std::int32_t prediction,
                                    std::int32_t difference,
                                    std::uint8_t bits_per_raw_sample) noexcept
{
    const std::int32_t reconstructed = prediction + difference;
    if (bits_per_raw_sample == 0 || bits_per_raw_sample >= 31) {
        return reconstructed;
    }

    const std::int32_t range = std::int32_t{1} << bits_per_raw_sample;
    std::int32_t wrapped = reconstructed % range;
    if (wrapped < 0) {
        wrapped += range;
    }
    return wrapped;
}

} // namespace ffv1::syntax

