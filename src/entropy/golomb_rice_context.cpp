#include "entropy/golomb_rice_context.hpp"

#include "util/rfc_math.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <cstdint>

namespace ffv1::entropy {

namespace {

Status validate_state(const GolombRiceContextState& state)
{
    if (state.count <= 0 || state.count > 128 || state.error_sum <= 0
        || state.drift < -state.error_sum || state.drift > state.error_sum
        || state.bias < -128 || state.bias > 127) {
        return make_error(ErrorCode::InvalidState, "Golomb-Rice context state is invalid");
    }
    return ok_status();
}

Status derive_k(const GolombRiceContextState& state, std::uint8_t& out_k) noexcept
{
    auto threshold = state.count;
    std::uint8_t k = 0;
    while (threshold < state.error_sum) {
        if (k == 31) {
            return make_error(ErrorCode::InvalidState,
                              "Golomb-Rice context requires an unrepresentable k parameter");
        }
        ++k;
        threshold *= 2;
    }
    out_k = k;
    return ok_status();
}

std::int32_t sign_extend(std::int64_t value, std::uint8_t bits) noexcept
{
    const auto range = std::uint64_t{1} << bits;
    const auto sign = range >> 1;
    const auto encoded = static_cast<std::uint64_t>(value) & (range - 1);
    return static_cast<std::int32_t>(encoded >= sign
                                         ? static_cast<std::int64_t>(encoded) - static_cast<std::int64_t>(range)
                                         : static_cast<std::int64_t>(encoded));
}

} // namespace

void GolombRiceContextState::reset() noexcept
{
    *this = {};
}

Status read_golomb_rice_symbol(GolombRiceReader& reader,
                               GolombRiceContextState& state,
                               std::uint8_t bits_per_raw_sample,
                               std::int32_t& out_value) noexcept
{
    Status status = validate_state(state);
    if (!status.ok()) {
        return status;
    }
    if (bits_per_raw_sample == 0 || bits_per_raw_sample > 31) {
        return make_error(ErrorCode::InvalidArgument,
                          "Golomb-Rice raw sample width must be in the range 1..31");
    }

    std::uint8_t k = 0;
    status = derive_k(state, k);
    if (!status.ok()) {
        return status;
    }
    std::int32_t coded_value = 0;
    status = reader.read_signed(k, bits_per_raw_sample, coded_value);
    if (!status.ok()) {
        return status;
    }

    std::int64_t value = coded_value;
    if (2 * state.drift < -state.count) {
        value = -1 - value;
    }
    const auto decoded = sign_extend(value + state.bias, bits_per_raw_sample);

    state.error_sum += value < 0 ? -value : value;
    state.drift += value;
    if (state.count == 128) {
        state.count >>= 1;
        state.drift = util::arithmetic_right_shift(state.drift, 1);
        state.error_sum >>= 1;
    }
    ++state.count;
    if (state.drift <= -state.count) {
        state.bias = std::max<std::int64_t>(state.bias - 1, -128);
        state.drift = std::max<std::int64_t>(state.drift + state.count,
                                             -state.count + 1);
    } else if (state.drift > 0) {
        state.bias = std::min<std::int64_t>(state.bias + 1, 127);
        state.drift = std::min<std::int64_t>(state.drift - state.count, 0);
    }

    out_value = decoded;
    return ok_status();
}

Status read_golomb_rice_run_interruption(GolombRiceReader& reader,
                                         GolombRiceContextState& state,
                                         std::uint8_t bits_per_raw_sample,
                                         std::int32_t& out_value) noexcept
{
    std::int32_t value = 0;
    Status status = read_golomb_rice_symbol(reader, state, bits_per_raw_sample, value);
    if (!status.ok()) {
        return status;
    }
    if (value >= 0) {
        ++value;
    }
    out_value = value;
    return ok_status();
}

} // namespace ffv1::entropy
