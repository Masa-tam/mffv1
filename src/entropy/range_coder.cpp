#include "entropy/range_coder.hpp"

#include "util/status.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace mffv1::entropy {

std::uint64_t SymbolReader::byte_position() const noexcept
{
    return 0;
}

Status RangeCoder::reset(ByteSpan payload,
                         std::size_t scalar_context_count,
                         std::uint8_t initial_state)
{
    const std::array context_counts{scalar_context_count};
    return reset(payload, context_counts, initial_state);
}

Status RangeCoder::reset(ByteSpan payload,
                         std::span<const std::size_t> scalar_context_counts,
                         std::uint8_t initial_state)
{
    return reset_impl(payload,
                      scalar_context_counts,
                      {},
                      initial_state,
                      syntax::kDefaultStateTransition);
}

Status RangeCoder::reset(
    ByteSpan payload,
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks)
{
    return reset_impl(payload,
                      scalar_context_counts,
                      initial_state_banks,
                      kDefaultInitialState,
                      syntax::kDefaultStateTransition);
}

Status RangeCoder::reset(ByteSpan payload, const syntax::StateTransitionTable& state_transition)
{
    const std::array<std::size_t, 1> context_counts{1};
    return reset_impl(payload, context_counts, {}, kDefaultInitialState, state_transition);
}

Status RangeCoder::reset(
    ByteSpan payload,
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    const syntax::StateTransitionTable& state_transition)
{
    return reset_impl(payload,
                      scalar_context_counts,
                      initial_state_banks,
                      kDefaultInitialState,
                      state_transition);
}

Status RangeCoder::reset_from_contexts(
    ByteSpan payload,
    const ContextStateBanks& context_banks,
    const syntax::StateTransitionTable& state_transition)
{
    std::vector<std::size_t> context_counts;
    std::vector<std::span<const ScalarContextStates>> context_bank_spans;
    context_counts.reserve(context_banks.size());
    context_bank_spans.reserve(context_banks.size());
    for (const auto& context_bank : context_banks) {
        context_counts.push_back(context_bank.size());
        context_bank_spans.emplace_back(context_bank);
    }
    return reset_impl(payload,
                      context_counts,
                      context_bank_spans,
                      kDefaultInitialState,
                      state_transition);
}

Status RangeCoder::reset_from_arithmetic_state(
    ByteSpan payload,
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    const syntax::StateTransitionTable& state_transition,
    const ArithmeticState& arithmetic_state)
{
    if (!arithmetic_state.initialized) {
        return make_error(ErrorCode::InvalidArgument,
                          "range coder arithmetic state is not initialized");
    }
    if (arithmetic_state.range == 0) {
        return make_error(ErrorCode::InvalidArgument,
                          "range coder arithmetic state range must be non-zero");
    }
    if (arithmetic_state.byte_position > payload.size()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "range coder arithmetic byte position is outside the payload",
                               arithmetic_state.byte_position);
    }

    Status status = initialize_contexts(scalar_context_counts,
                                        initial_state_banks,
                                        kDefaultInitialState);
    if (!status.ok()) {
        return status;
    }

    payload_ = payload;
    range_ = arithmetic_state.range;
    low_ = arithmetic_state.low;
    byte_position_ = arithmetic_state.byte_position;
    end_ = arithmetic_state.end || byte_position_ >= payload_.size();
    initialized_ = true;
    state_transition_ = state_transition;
    return ok_status();
}

Status RangeCoder::reset_impl(
    ByteSpan payload,
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    std::uint8_t initial_state,
    const syntax::StateTransitionTable& state_transition)
{
    if (payload.size() < 2) {
        return make_byte_error(ErrorCode::SyntaxError, "range coder payload must contain at least two bytes", 0);
    }

    Status status = initialize_contexts(scalar_context_counts, initial_state_banks, initial_state);
    if (!status.ok()) {
        return status;
    }

    payload_ = payload;
    range_ = 0xff00;
    low_ = (static_cast<std::uint32_t>(payload_[0]) << 8)
        | static_cast<std::uint32_t>(payload_[1]);
    byte_position_ = 2;
    end_ = byte_position_ >= payload_.size();
    if (low_ >= range_) {
        low_ = range_;
        end_ = true;
    }

    state_transition_ = state_transition;
    initialized_ = true;
    return ok_status();
}

Status RangeCoder::reconfigure_contexts(
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
    }
    return initialize_contexts(scalar_context_counts,
                               initial_state_banks,
                               kDefaultInitialState);
}

Status RangeCoder::copy_contexts(ContextStateBanks& out_context_banks) const
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
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

Status RangeCoder::initialize_contexts(
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    std::uint8_t initial_state)
{
    if (scalar_context_counts.empty()) {
        return make_error(ErrorCode::InvalidArgument, "range coder must have at least one context bank");
    }
    if (scalar_context_counts.size() > kMaxContextBankCount) {
        return make_error(ErrorCode::ResourceExhausted, "range coder context bank count exceeds the supported limit");
    }
    if (!initial_state_banks.empty() && initial_state_banks.size() != scalar_context_counts.size()) {
        return make_error(ErrorCode::InvalidArgument,
                          "range coder initial state bank count does not match context bank count");
    }

    std::size_t total_context_count = 0;
    for (std::size_t bank = 0; bank < scalar_context_counts.size(); ++bank) {
        const auto context_count = scalar_context_counts[bank];
        if (context_count == 0) {
            return make_error(ErrorCode::InvalidArgument,
                              "range coder context banks must have at least one scalar context");
        }
        if (context_count > kMaxScalarContextCount) {
            return make_error(ErrorCode::ResourceExhausted,
                              "range coder scalar context count exceeds the supported limit");
        }
        if (!initial_state_banks.empty() && !initial_state_banks[bank].empty()
            && initial_state_banks[bank].size() != context_count) {
            return make_error(ErrorCode::InvalidArgument,
                              "range coder initial state count does not match scalar context count");
        }
        total_context_count += context_count;
    }

    scalar_context_bank_offsets_.clear();
    scalar_context_bank_offsets_.reserve(scalar_context_counts.size());
    scalar_context_bank_sizes_.assign(scalar_context_counts.begin(), scalar_context_counts.end());
    std::size_t context_offset = 0;
    for (const auto context_count : scalar_context_counts) {
        scalar_context_bank_offsets_.push_back(context_offset);
        context_offset += context_count;
    }
    scalar_contexts_.assign(total_context_count, {});
    for (std::size_t bank = 0; bank < scalar_context_counts.size(); ++bank) {
        const auto offset = scalar_context_bank_offsets_[bank];
        if (!initial_state_banks.empty() && !initial_state_banks[bank].empty()) {
            std::copy(initial_state_banks[bank].begin(),
                      initial_state_banks[bank].end(),
                      scalar_contexts_.begin() + static_cast<std::ptrdiff_t>(offset));
        } else {
            for (std::size_t context = 0; context < scalar_context_counts[bank]; ++context) {
                scalar_contexts_[offset + context].fill(initial_state);
            }
        }
    }
    return ok_status();
}

std::uint64_t RangeCoder::byte_position() const noexcept
{
    return byte_position_;
}

RangeCoder::ArithmeticState RangeCoder::arithmetic_state() const noexcept
{
    return ArithmeticState{
        range_,
        low_,
        byte_position_,
        end_,
        initialized_,
    };
}

Status RangeCoder::read_bool(bool& out_value)
{
    if (scalar_context_bank_sizes_.empty() || scalar_context_bank_sizes_[0] == 0) {
        return make_error(ErrorCode::InvalidState,
                          "range coder has no scalar context for binary symbols");
    }
    auto& states = scalar_contexts_[scalar_context_bank_offsets_[0]];
    return read_rac(states[0], out_value);
}

Status RangeCoder::read_termination_sentinel()
{
    std::uint8_t state = 129;
    bool discarded = false;
    return read_rac(state, discarded);
}

Status RangeCoder::read_unsigned(std::uint64_t& out_value)
{
    std::int64_t value = 0;
    Status status = read_symbol(0, 0, false, value);
    if (!status.ok()) {
        return status;
    }
    out_value = static_cast<std::uint64_t>(value);
    return ok_status();
}

Status RangeCoder::read_signed(std::int64_t& out_value)
{
    return read_symbol(0, 0, true, out_value);
}

Status RangeCoder::read_unsigned(ContextId context, std::uint64_t& out_value)
{
    std::int64_t value = 0;
    Status status = read_symbol(0, context, false, value);
    if (!status.ok()) {
        return status;
    }
    out_value = static_cast<std::uint64_t>(value);
    return ok_status();
}

Status RangeCoder::read_signed(ContextId context, std::int64_t& out_value)
{
    return read_symbol(0, context, true, out_value);
}

Status RangeCoder::read_unsigned(std::size_t context_bank,
                                 ContextId context,
                                 std::uint64_t& out_value)
{
    std::int64_t value = 0;
    Status status = read_symbol(context_bank, context, false, value);
    if (!status.ok()) {
        return status;
    }
    out_value = static_cast<std::uint64_t>(value);
    return ok_status();
}

Status RangeCoder::read_signed(std::size_t context_bank,
                               ContextId context,
                               std::int64_t& out_value)
{
    return read_symbol(context_bank, context, true, out_value);
}

Status RangeCoder::set_state_transition(
    const syntax::StateTransitionTable& state_transition)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
    }
    state_transition_ = state_transition;
    return ok_status();
}

Status RangeCoder::set_legacy_v0_arithmetic(bool enabled)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
    }
    legacy_v0_arithmetic_ = enabled;
    if (enabled) {
        for (auto& context : scalar_contexts_) {
            context[0] = 255;
        }
    }
    return ok_status();
}

Status RangeCoder::begin_independent_scalar_contexts(std::size_t scalar_context_count)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
    }

    scalar_context_snapshots_.push_back({
        scalar_context_bank_offsets_,
        scalar_context_bank_sizes_,
        scalar_contexts_,
    });
    const std::array context_counts{scalar_context_count};
    Status status = initialize_contexts(context_counts, {}, kDefaultInitialState);
    if (!status.ok()) {
        const auto snapshot = std::move(scalar_context_snapshots_.back());
        scalar_context_snapshots_.pop_back();
        scalar_context_bank_offsets_ = std::move(snapshot.bank_offsets);
        scalar_context_bank_sizes_ = std::move(snapshot.bank_sizes);
        scalar_contexts_ = std::move(snapshot.contexts);
        return status;
    }
    return ok_status();
}

Status RangeCoder::end_independent_scalar_contexts()
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
    }
    if (scalar_context_snapshots_.empty()) {
        return make_error(ErrorCode::InvalidState, "range coder scalar context snapshot stack is empty");
    }

    auto snapshot = std::move(scalar_context_snapshots_.back());
    scalar_context_snapshots_.pop_back();
    scalar_context_bank_offsets_ = std::move(snapshot.bank_offsets);
    scalar_context_bank_sizes_ = std::move(snapshot.bank_sizes);
    scalar_contexts_ = std::move(snapshot.contexts);
    return ok_status();
}

Status RangeCoder::read_rac(std::uint8_t& state, bool& out_bit)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
    }

    const auto product = static_cast<std::uint64_t>(range_) * state;
    const std::uint32_t rangeoff = static_cast<std::uint32_t>(
        (product + (legacy_v0_arithmetic_ ? 255u : 0u)) >> 8);
    const auto nonzero_product = static_cast<std::uint64_t>(range_)
        * (256u - state)
        + (legacy_v0_arithmetic_ ? 0u : 255u);
    const auto one_path_carry = legacy_v0_arithmetic_
        ? (((product + 255u) & 0xffu) != 0 ? 1u : 0u)
            + ((nonzero_product & 0xffu) != 0 ? 1u : 0u)
        : 0u;
    range_ -= rangeoff;
    if (low_ < range_) {
        state = syntax::range_zero_state(state_transition_, state);
        out_bit = false;
        refill();
        return ok_status();
    }

    low_ -= range_;
    range_ = rangeoff;
    state = syntax::range_one_state(state_transition_, state);
    out_bit = true;
    refill();
    low_ += one_path_carry;
    return ok_status();
}

Status RangeCoder::read_symbol(std::size_t context_bank,
                               ContextId context,
                               bool is_signed,
                               std::int64_t& out_value)
{
    if (context_bank >= scalar_context_bank_sizes_.size()) {
        return make_error(ErrorCode::InvalidArgument, "range coder scalar context bank is out of range");
    }
    if (context >= scalar_context_bank_sizes_[context_bank]) {
        return make_error(ErrorCode::InvalidArgument, "range coder scalar context is out of range");
    }

    auto& states = scalar_contexts_[scalar_context_bank_offsets_[context_bank] + context];

    bool bit = false;
    Status status = read_rac(states[0], bit);
    if (!status.ok()) {
        return status;
    }
    if (bit) {
        out_value = 0;
        return ok_status();
    }

    std::uint32_t exponent = 0;
    for (;;) {
        const std::size_t state_index = 1 + std::min<std::uint32_t>(exponent, 9);
        status = read_rac(states[state_index], bit);
        if (!status.ok()) {
            return status;
        }
        if (!bit) {
            break;
        }
        ++exponent;
        if (exponent > 62) {
            return make_error(ErrorCode::SyntaxError, "range coded scalar exponent is too large");
        }
    }

    std::uint64_t magnitude = 1;
    for (std::int32_t i = static_cast<std::int32_t>(exponent) - 1; i >= 0; --i) {
        const std::size_t state_index = 22 + std::min<std::int32_t>(i, 9);
        status = read_rac(states[state_index], bit);
        if (!status.ok()) {
            return status;
        }
        magnitude = (magnitude << 1) | (bit ? 1u : 0u);
    }

    if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return make_error(ErrorCode::SyntaxError, "range coded scalar magnitude is too large");
    }

    auto signed_value = static_cast<std::int64_t>(magnitude);
    if (is_signed) {
        const std::size_t state_index = 11 + std::min<std::uint32_t>(exponent, 10);
        status = read_rac(states[state_index], bit);
        if (!status.ok()) {
            return status;
        }
        if (bit) {
            signed_value = -signed_value;
        }
    }

    out_value = signed_value;
    return ok_status();
}

void RangeCoder::refill() noexcept
{
    if (range_ >= 256) {
        return;
    }

    range_ <<= 8;
    low_ <<= 8;
    if (!end_) {
        if (byte_position_ < payload_.size()) {
            low_ += static_cast<std::uint32_t>(payload_[static_cast<std::size_t>(byte_position_)]);
            ++byte_position_;
        }
        if (byte_position_ >= payload_.size()) {
            end_ = true;
        }
    }
}

} // namespace mffv1::entropy
