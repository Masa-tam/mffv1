#include "entropy/range_encoder.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace mffv1::entropy {

Status RangeEncoder::reset(const syntax::StateTransitionTable& state_transition)
{
    const std::array<std::size_t, 1> context_counts{1};
    return reset_impl(context_counts, {}, kDefaultInitialState, state_transition);
}

Status RangeEncoder::reset(std::size_t scalar_context_count,
                           std::uint8_t initial_state)
{
    const std::array context_counts{scalar_context_count};
    return reset(context_counts, initial_state);
}

Status RangeEncoder::reset(std::span<const std::size_t> scalar_context_counts,
                           std::uint8_t initial_state)
{
    return reset_impl(scalar_context_counts,
                      {},
                      initial_state,
                      syntax::kDefaultStateTransition);
}

Status RangeEncoder::reset(
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    const syntax::StateTransitionTable& state_transition)
{
    return reset_impl(scalar_context_counts,
                      initial_state_banks,
                      kDefaultInitialState,
                      state_transition);
}

Status RangeEncoder::reset_impl(
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    std::uint8_t initial_state,
    const syntax::StateTransitionTable& state_transition)
{
    RangeEncoder next;
    Status status = next.initialize_contexts(
        scalar_context_counts, initial_state_banks, initial_state);
    if (!status.ok()) {
        return status;
    }
    next.low_bytes_.assign(2, std::byte{0});
    next.range_ = 0xff00;
    next.state_transition_ = state_transition;
    next.initialized_ = true;
    *this = std::move(next);
    return ok_status();
}

Status RangeEncoder::initialize_contexts(
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    std::uint8_t initial_state)
{
    if (scalar_context_counts.empty()) {
        return make_error(ErrorCode::InvalidArgument,
                          "range encoder must have at least one context bank");
    }
    if (scalar_context_counts.size() > kMaxContextBankCount) {
        return make_error(ErrorCode::ResourceExhausted,
                          "range encoder context bank count exceeds the supported limit");
    }
    if (!initial_state_banks.empty()
        && initial_state_banks.size() != scalar_context_counts.size()) {
        return make_error(ErrorCode::InvalidArgument,
                          "range encoder initial state bank count does not match context bank count");
    }

    std::size_t total_context_count = 0;
    for (std::size_t bank = 0; bank < scalar_context_counts.size(); ++bank) {
        const auto context_count = scalar_context_counts[bank];
        if (context_count == 0) {
            return make_error(ErrorCode::InvalidArgument,
                              "range encoder context banks must have at least one scalar context");
        }
        if (context_count > kMaxScalarContextCount) {
            return make_error(ErrorCode::ResourceExhausted,
                              "range encoder scalar context count exceeds the supported limit");
        }
        if (total_context_count > std::numeric_limits<std::size_t>::max() - context_count) {
            return make_error(ErrorCode::ResourceExhausted,
                              "range encoder total context count overflows size_t");
        }
        if (!initial_state_banks.empty() && !initial_state_banks[bank].empty()
            && initial_state_banks[bank].size() != context_count) {
            return make_error(ErrorCode::InvalidArgument,
                              "range encoder initial state count does not match scalar context count");
        }
        total_context_count += context_count;
    }

    std::vector<std::size_t> offsets;
    offsets.reserve(scalar_context_counts.size());
    std::size_t context_offset = 0;
    for (const auto context_count : scalar_context_counts) {
        offsets.push_back(context_offset);
        context_offset += context_count;
    }
    std::vector<std::size_t> sizes(
        scalar_context_counts.begin(), scalar_context_counts.end());
    std::vector<ScalarContextStates> contexts(total_context_count);
    for (std::size_t bank = 0; bank < scalar_context_counts.size(); ++bank) {
        const auto offset = offsets[bank];
        if (!initial_state_banks.empty() && !initial_state_banks[bank].empty()) {
            std::copy(initial_state_banks[bank].begin(),
                      initial_state_banks[bank].end(),
                      contexts.begin() + static_cast<std::ptrdiff_t>(offset));
        } else {
            for (std::size_t context = 0; context < scalar_context_counts[bank]; ++context) {
                contexts[offset + context].fill(initial_state);
            }
        }
    }

    scalar_context_bank_offsets_ = std::move(offsets);
    scalar_context_bank_sizes_ = std::move(sizes);
    scalar_contexts_ = std::move(contexts);
    return ok_status();
}

Status RangeEncoder::reconfigure_contexts(
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }

    RangeEncoder next = *this;
    Status status = next.initialize_contexts(
        scalar_context_counts, initial_state_banks, kDefaultInitialState);
    if (!status.ok()) {
        return status;
    }
    *this = std::move(next);
    return ok_status();
}

Status RangeEncoder::copy_contexts(ContextStateBanks& out_context_banks) const
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }

    ContextStateBanks context_banks;
    context_banks.reserve(scalar_context_bank_sizes_.size());
    for (std::size_t bank = 0; bank < scalar_context_bank_sizes_.size(); ++bank) {
        const auto offset = scalar_context_bank_offsets_[bank];
        const auto size = scalar_context_bank_sizes_[bank];
        const auto begin = scalar_contexts_.begin() + static_cast<std::ptrdiff_t>(offset);
        context_banks.emplace_back(begin, begin + static_cast<std::ptrdiff_t>(size));
    }
    out_context_banks = std::move(context_banks);
    return ok_status();
}

Status RangeEncoder::begin_independent_scalar_contexts(std::size_t scalar_context_count)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }

    scalar_context_snapshots_.push_back({
        scalar_context_bank_offsets_,
        scalar_context_bank_sizes_,
        scalar_contexts_,
    });
    const std::array context_counts{scalar_context_count};
    Status status = initialize_contexts(context_counts, {}, kDefaultInitialState);
    if (!status.ok()) {
        auto snapshot = std::move(scalar_context_snapshots_.back());
        scalar_context_snapshots_.pop_back();
        scalar_context_bank_offsets_ = std::move(snapshot.bank_offsets);
        scalar_context_bank_sizes_ = std::move(snapshot.bank_sizes);
        scalar_contexts_ = std::move(snapshot.contexts);
        return status;
    }
    return ok_status();
}

Status RangeEncoder::end_independent_scalar_contexts()
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }
    if (scalar_context_snapshots_.empty()) {
        return make_error(ErrorCode::InvalidState, "range encoder scalar context snapshot stack is empty");
    }

    auto snapshot = std::move(scalar_context_snapshots_.back());
    scalar_context_snapshots_.pop_back();
    scalar_context_bank_offsets_ = std::move(snapshot.bank_offsets);
    scalar_context_bank_sizes_ = std::move(snapshot.bank_sizes);
    scalar_contexts_ = std::move(snapshot.contexts);
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

    if (scalar_context_bank_sizes_.empty() || scalar_context_bank_sizes_[0] == 0) {
        return make_error(ErrorCode::InvalidState,
                          "range encoder has no scalar context for binary symbols");
    }
    auto& states = scalar_contexts_[scalar_context_bank_offsets_[0]];
    return write_rac(states[0], value);
}

Status RangeEncoder::write_termination_sentinel()
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }

    std::uint8_t state = 129;
    return write_rac(state, false);
}

Status RangeEncoder::write_rac(std::uint8_t& state, bool value)
{
    const std::uint32_t range_offset = (range_ * state) >> 8;
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
        state = syntax::range_one_state(state_transition_, state);
    } else {
        state = syntax::range_zero_state(state_transition_, state);
    }
    range_ = next_range;

    if (renormalize) {
        range_ <<= 8;
        low_bytes_.push_back(std::byte{0});
    }
    return ok_status();
}

Status RangeEncoder::write_unsigned(std::uint64_t value)
{
    return write_unsigned(0, 0, value);
}

Status RangeEncoder::write_signed(std::int64_t value)
{
    return write_signed(0, 0, value);
}

Status RangeEncoder::write_unsigned(ContextId context, std::uint64_t value)
{
    return write_unsigned(0, context, value);
}

Status RangeEncoder::write_signed(ContextId context, std::int64_t value)
{
    return write_signed(0, context, value);
}

Status RangeEncoder::write_unsigned(std::size_t context_bank,
                                    ContextId context,
                                    std::uint64_t value)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }
    if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return make_error(ErrorCode::InvalidArgument,
                          "unsigned value exceeds the range coder scalar limit");
    }
    return write_symbol(context_bank, context, false, value, false);
}

Status RangeEncoder::write_signed(std::size_t context_bank,
                                  ContextId context,
                                  std::int64_t value)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is finalized");
    }
    if (value == std::numeric_limits<std::int64_t>::min()) {
        return make_error(ErrorCode::InvalidArgument,
                          "signed value magnitude exceeds the range coder scalar limit");
    }
    const bool negative = value < 0;
    const auto magnitude = static_cast<std::uint64_t>(negative ? -value : value);
    return write_symbol(context_bank, context, true, magnitude, negative);
}

Status RangeEncoder::write_symbol(std::size_t context_bank,
                                  ContextId context,
                                  bool is_signed,
                                  std::uint64_t magnitude,
                                  bool negative)
{
    if (context_bank >= scalar_context_bank_sizes_.size()) {
        return make_error(ErrorCode::InvalidArgument,
                          "range encoder scalar context bank is out of range");
    }
    if (context >= scalar_context_bank_sizes_[context_bank]) {
        return make_error(ErrorCode::InvalidArgument,
                          "range encoder scalar context is out of range");
    }

    auto& states = scalar_contexts_[
        scalar_context_bank_offsets_[context_bank] + context];

    struct Decision {
        std::size_t state_index = 0;
        bool value = false;
    };
    std::array<Decision, 127> decisions{};
    std::size_t decision_count = 0;
    decisions[decision_count++] = {0, magnitude == 0};

    if (magnitude != 0) {
        const auto exponent =
            static_cast<std::uint32_t>(std::bit_width(magnitude) - 1);
        for (std::uint32_t index = 0; index < exponent; ++index) {
            decisions[decision_count++] = {
                1 + std::min<std::uint32_t>(index, 9),
                true,
            };
        }
        decisions[decision_count++] = {
            1 + std::min<std::uint32_t>(exponent, 9),
            false,
        };

        for (std::int32_t bit = static_cast<std::int32_t>(exponent) - 1;
             bit >= 0;
             --bit) {
            decisions[decision_count++] = {
                static_cast<std::size_t>(
                    22 + std::min<std::int32_t>(bit, 9)),
                ((magnitude >> bit) & 1u) != 0,
            };
        }

        if (is_signed) {
            decisions[decision_count++] = {
                11 + std::min<std::uint32_t>(exponent, 10),
                negative,
            };
        }
    }

    auto trial_states = states;
    auto trial_range = range_;
    std::size_t appended_bytes = 0;
    for (std::size_t index = 0; index < decision_count; ++index) {
        const auto& decision = decisions[index];
        auto& state = trial_states[decision.state_index];
        const std::uint32_t range_offset = (trial_range * state) >> 8;
        const std::uint32_t zero_range = trial_range - range_offset;
        const std::uint32_t next_range =
            decision.value ? range_offset : zero_range;
        if (next_range == 0) {
            return make_error(
                ErrorCode::InvalidArgument,
                "scalar symbol enters a zero-width range in the current context state");
        }
        state = decision.value
            ? syntax::range_one_state(state_transition_, state)
            : syntax::range_zero_state(state_transition_, state);
        trial_range = next_range;
        if (trial_range < 256) {
            trial_range <<= 8;
            ++appended_bytes;
        }
    }

    if (appended_bytes > low_bytes_.max_size() - low_bytes_.size()) {
        return make_error(ErrorCode::ResourceExhausted,
                          "range encoder output exceeds vector capacity");
    }
    low_bytes_.reserve(low_bytes_.size() + appended_bytes);
    for (std::size_t index = 0; index < decision_count; ++index) {
        const auto& decision = decisions[index];
        Status status = write_rac(states[decision.state_index], decision.value);
        if (!status.ok()) {
            return make_error(ErrorCode::InternalError,
                              "prevalidated scalar range decision failed");
        }
    }
    return ok_status();
}

Status RangeEncoder::finalize(std::vector<std::byte>& out_bytes)
{
    return finalize_impl(out_bytes, false);
}

Status RangeEncoder::finalize_closed(std::vector<std::byte>& out_bytes)
{
    return finalize_impl(out_bytes, true);
}

Status RangeEncoder::finalize_impl(std::vector<std::byte>& out_bytes,
                                   bool close_interval)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is not initialized");
    }
    if (finalized_) {
        return make_error(ErrorCode::InvalidState, "range encoder is already finalized");
    }

    auto completed = *this;
    if (close_interval) {
        const auto last_byte = completed.low_bytes_.empty()
            ? std::uint32_t{0}
            : static_cast<std::uint32_t>(
                static_cast<std::uint8_t>(completed.low_bytes_.back()));
        auto delta_to_zero_low_byte = (256u - last_byte) & 0xffu;
        if (delta_to_zero_low_byte == 0 && completed.range_ > 256u) {
            delta_to_zero_low_byte = 256u;
        }
        const auto closing_delta = delta_to_zero_low_byte < completed.range_
            ? delta_to_zero_low_byte
            : completed.range_ / 2u;
        Status status = completed.add_to_low(closing_delta);
        if (!status.ok()) {
            return status;
        }
    }
    auto bytes = completed.low_bytes_;
    if (close_interval
        && bytes.size() > 2
        && static_cast<std::uint8_t>(bytes.back()) == 0) {
        bytes.pop_back();
    }
    out_bytes = std::move(bytes);
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
