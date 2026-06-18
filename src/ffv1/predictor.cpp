#include "ffv1/predictor.hpp"

#include "util/rfc_math.hpp"

namespace ffv1::syntax {

namespace {

std::int32_t as_signed_16bit(std::int32_t sample) noexcept
{
    return sample >= 32768 ? sample - 65536 : sample;
}

} // namespace

std::int32_t Predictor::median_predict(std::int32_t left,
                                       std::int32_t top,
                                       std::int32_t top_left) noexcept
{
    return util::median3(left, top, left + top - top_left);
}

std::int32_t Predictor::median_predict_signed_16bit(std::int32_t left,
                                                    std::int32_t top,
                                                    std::int32_t top_left) noexcept
{
    const auto signed_left = as_signed_16bit(left);
    const auto signed_top = as_signed_16bit(top);
    const auto signed_top_left = as_signed_16bit(top_left);
    return util::median3(signed_left,
                         signed_top,
                         signed_left + signed_top - signed_top_left);
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
