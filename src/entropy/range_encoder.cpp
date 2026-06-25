#include "entropy/range_encoder.hpp"

#include <utility>

namespace mffv1::entropy {

Status RangeEncoder::reset(const syntax::StateTransitionTable& state_transition)
{
    std::vector<std::byte> initial_low(2, std::byte{0});
    low_bytes_ = std::move(initial_low);
    range_ = 0xff00;
    bool_state_ = kDefaultInitialState;
    state_transition_ = state_transition;
    initialized_ = true;
    finalized_ = false;
    return ok_status();
}

Status RangeEncoder::write_bool(bool value)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }

    const std::uint32_t range_offset = (range_ * bool_state_) >> 8;
    const std::uint32_t zero_range = range_ - range_offset;
    const std::uint32_t next_range = value ? range_offset : zero_range;
    if (next_range == 0) {
        return make_error(ErrorCode::InvalidArgument,
                          "binary value has a zero-width range in the current state");
    }
    const bool renormalize = next_range < 256;
    if (renormalize && low_bytes_.size() == low_bytes_.max_size()) {
        return make_error(ErrorCode::ResourceExhausted, "range encoder output exceeds vector capacity");
    }

    if (value) {
        Status status = add_to_low(zero_range);
        if (!status.ok()) {
            return status;
        }
        bool_state_ = syntax::range_one_state(state_transition_, bool_state_);
    } else {
        bool_state_ = syntax::range_zero_state(state_transition_, bool_state_);
    }
    range_ = next_range;

    if (renormalize) {
        range_ <<= 8;
        low_bytes_.push_back(std::byte{0});
    }
    return ok_status();
}

Status RangeEncoder::write_unsigned(std::uint64_t)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }
    return make_error(ErrorCode::NotImplemented,
                      "range encoding of unsigned symbols is not implemented yet");
}

Status RangeEncoder::write_signed(std::int64_t)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }
    return make_error(ErrorCode::NotImplemented,
                      "range encoding of signed symbols is not implemented yet");
}

Status RangeEncoder::finalize(std::vector<std::byte>& out_bytes)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is already finalized");
    }

    auto completed = low_bytes_;
    out_bytes = std::move(completed);
    finalized_ = true;
    return ok_status();
}

bool RangeEncoder::initialized() const noexcept
{
    return initialized_;
}

bool RangeEncoder::finalized() const noexcept
{
    return finalized_;
}

std::size_t RangeEncoder::byte_count() const noexcept
{
    return low_bytes_.size();
}

Status RangeEncoder::add_to_low(std::uint32_t value) noexcept
{
    std::size_t index = low_bytes_.size();
    std::uint32_t carry = value;
    while (carry != 0 && index != 0) {
        --index;
        const std::uint32_t sum =
            static_cast<std::uint8_t>(low_bytes_[index]) + (carry & 0xffu);
        low_bytes_[index] = static_cast<std::byte>(sum & 0xffu);
        carry = (carry >> 8) + (sum >> 8);
    }
    if (carry != 0) {
        return make_error(ErrorCode::InternalError,
                          "range encoder lower bound exceeded its precision");
    }
    return ok_status();
}

} // namespace mffv1::entropy
