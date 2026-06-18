#include "entropy/range_coder.hpp"

#include "util/status.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ffv1::entropy {

std::uint64_t SymbolReader::byte_position() const noexcept
{
    return 0;
}

namespace {

constexpr std::array<std::uint8_t, 256> kDefaultStateTransition = {
    0, 0, 0, 0, 0, 0, 0, 0, 20, 21, 22, 23, 24, 25, 26, 27,
    28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 56, 57,
    58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
    74, 75, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 93, 94, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 114, 115, 116, 117, 118,
    119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 133,
    134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 190, 191, 192, 194, 194,
    195, 196, 197, 198, 199, 200, 201, 202, 202, 204, 205, 206, 207, 208, 209, 209,
    210, 211, 212, 213, 215, 215, 216, 217, 218, 219, 220, 220, 222, 223, 224, 225,
    226, 227, 227, 229, 229, 230, 231, 232, 234, 234, 235, 236, 237, 238, 239, 240,
    241, 242, 243, 244, 245, 246, 247, 248, 248, 0, 0, 0, 0, 0, 0, 0,
};

std::uint8_t zero_state(std::uint8_t state) noexcept
{
    if (state == 0) {
        return 0;
    }
    return static_cast<std::uint8_t>(256u - kDefaultStateTransition[256u - state]);
}

std::uint8_t one_state(std::uint8_t state) noexcept
{
    return kDefaultStateTransition[state];
}

} // namespace

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
    return reset_impl(payload, scalar_context_counts, {}, initial_state);
}

Status RangeCoder::reset(
    ByteSpan payload,
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks)
{
    return reset_impl(payload, scalar_context_counts, initial_state_banks, kDefaultInitialState);
}

Status RangeCoder::reset_impl(
    ByteSpan payload,
    std::span<const std::size_t> scalar_context_counts,
    std::span<const std::span<const ScalarContextStates>> initial_state_banks,
    std::uint8_t initial_state)
{
    if (payload.size() < 2) {
        return make_byte_error(ErrorCode::SyntaxError, "range coder payload must contain at least two bytes", 0);
    }
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

    bool_state_ = initial_state;
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
    initialized_ = true;
    return ok_status();
}

std::uint64_t RangeCoder::byte_position() const noexcept
{
    return byte_position_;
}

Status RangeCoder::read_bool(bool& out_value)
{
    return read_rac(bool_state_, out_value);
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

Status RangeCoder::read_rac(std::uint8_t& state, bool& out_bit)
{
    if (!initialized_) {
        return make_error(ErrorCode::InvalidState, "range coder is not initialized");
    }

    const std::uint32_t rangeoff = (range_ * state) >> 8;
    range_ -= rangeoff;
    if (low_ < range_) {
        state = zero_state(state);
        out_bit = false;
        refill();
        return ok_status();
    }

    low_ -= range_;
    range_ = rangeoff;
    state = one_state(state);
    out_bit = true;
    refill();
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

} // namespace ffv1::entropy
