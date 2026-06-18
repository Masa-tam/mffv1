#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "entropy/symbol_reader.hpp"
#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"

namespace ffv1::entropy {

class RangeCoder final : public SymbolReader {
public:
    static constexpr std::size_t kScalarContextSize = 32;
    static constexpr std::size_t kMaxScalarContextCount = 32768;
    static constexpr std::size_t kMaxContextBankCount = 4;
    static constexpr std::uint8_t kDefaultInitialState = 128;

    Status reset(ByteSpan payload,
                 std::size_t scalar_context_count = 1,
                 std::uint8_t initial_state = kDefaultInitialState);
    Status reset(ByteSpan payload,
                 std::span<const std::size_t> scalar_context_counts,
                 std::uint8_t initial_state = kDefaultInitialState);

    [[nodiscard]] std::uint64_t byte_position() const noexcept override;

    Status read_bool(bool& out_value) override;
    Status read_unsigned(std::uint64_t& out_value) override;
    Status read_signed(std::int64_t& out_value) override;

    Status read_unsigned(ContextId context, std::uint64_t& out_value);
    Status read_signed(ContextId context, std::int64_t& out_value);
    Status read_unsigned(std::size_t context_bank, ContextId context, std::uint64_t& out_value);
    Status read_signed(std::size_t context_bank, ContextId context, std::int64_t& out_value);

private:
    Status read_rac(std::uint8_t& state, bool& out_bit);
    Status read_symbol(std::size_t context_bank,
                       ContextId context,
                       bool is_signed,
                       std::int64_t& out_value);
    void refill() noexcept;

    ByteSpan payload_;
    std::uint32_t range_ = 0;
    std::uint32_t low_ = 0;
    std::uint64_t byte_position_ = 0;
    bool end_ = false;
    bool initialized_ = false;
    std::uint8_t bool_state_ = kDefaultInitialState;
    std::vector<std::size_t> scalar_context_bank_offsets_;
    std::vector<std::size_t> scalar_context_bank_sizes_;
    std::vector<std::array<std::uint8_t, kScalarContextSize>> scalar_contexts_;
};

} // namespace ffv1::entropy
