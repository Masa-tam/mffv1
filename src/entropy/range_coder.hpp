#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "entropy/symbol_reader.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/state_transition.hpp"

namespace mffv1::entropy {

class RangeCoder final : public SymbolReader {
public:
    static constexpr std::size_t kScalarContextSize = 32;
    using ScalarContextStates = std::array<std::uint8_t, kScalarContextSize>;
    static constexpr std::size_t kMaxScalarContextCount = 32768;
    static constexpr std::size_t kMaxContextBankCount = 4;
    static constexpr std::uint8_t kDefaultInitialState = 128;

    Status reset(ByteSpan payload,
                 std::size_t scalar_context_count = 1,
                 std::uint8_t initial_state = kDefaultInitialState);
    Status reset(ByteSpan payload,
                 std::span<const std::size_t> scalar_context_counts,
                 std::uint8_t initial_state = kDefaultInitialState);
    Status reset(ByteSpan payload,
                 std::span<const std::size_t> scalar_context_counts,
                 std::span<const std::span<const ScalarContextStates>> initial_state_banks);
    Status reset(ByteSpan payload, const syntax::StateTransitionTable& state_transition);
    Status reset(ByteSpan payload,
                 std::span<const std::size_t> scalar_context_counts,
                 std::span<const std::span<const ScalarContextStates>> initial_state_banks,
                 const syntax::StateTransitionTable& state_transition);
    Status reconfigure_contexts(
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks = {});

    [[nodiscard]] std::uint64_t byte_position() const noexcept override;

    Status read_bool(bool& out_value) override;
    Status read_unsigned(std::uint64_t& out_value) override;
    Status read_signed(std::int64_t& out_value) override;

    Status read_unsigned(ContextId context, std::uint64_t& out_value);
    Status read_signed(ContextId context, std::int64_t& out_value) override;
    Status read_unsigned(std::size_t context_bank, ContextId context, std::uint64_t& out_value);
    Status read_signed(std::size_t context_bank, ContextId context, std::int64_t& out_value);

private:
    Status reset_impl(ByteSpan payload,
                      std::span<const std::size_t> scalar_context_counts,
                      std::span<const std::span<const ScalarContextStates>> initial_state_banks,
                      std::uint8_t initial_state,
                      const syntax::StateTransitionTable& state_transition);
    Status initialize_contexts(
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks,
        std::uint8_t initial_state);
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
    syntax::StateTransitionTable state_transition_ = syntax::kDefaultStateTransition;
    std::vector<std::size_t> scalar_context_bank_offsets_;
    std::vector<std::size_t> scalar_context_bank_sizes_;
    std::vector<ScalarContextStates> scalar_contexts_;
};

} // namespace mffv1::entropy
