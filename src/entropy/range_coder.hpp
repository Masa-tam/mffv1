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
    using ContextStateBank = std::vector<ScalarContextStates>;
    using ContextStateBanks = std::vector<ContextStateBank>;
    static constexpr std::size_t kMaxScalarContextCount = 32768;
    static constexpr std::size_t kMaxContextBankCount = 4;
    static constexpr std::uint8_t kDefaultInitialState = 128;

    struct ArithmeticState {
        std::uint32_t range = 0;
        std::uint32_t low = 0;
        std::uint64_t byte_position = 0;
        bool end = false;
        bool initialized = false;

        friend bool operator==(const ArithmeticState&,
                               const ArithmeticState&) = default;
    };

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
    Status reset_from_contexts(
        ByteSpan payload,
        const ContextStateBanks& context_banks,
        const syntax::StateTransitionTable& state_transition = syntax::kDefaultStateTransition);
    Status reset_from_arithmetic_state(
        ByteSpan payload,
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks,
        const syntax::StateTransitionTable& state_transition,
        const ArithmeticState& arithmetic_state);
    Status reconfigure_contexts(
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks = {});
    Status copy_contexts(ContextStateBanks& out_context_banks) const;

    [[nodiscard]] std::uint64_t byte_position() const noexcept override;
    [[nodiscard]] ArithmeticState arithmetic_state() const noexcept;

    Status read_bool(bool& out_value) override;
    Status read_termination_sentinel();
    Status read_unsigned(std::uint64_t& out_value) override;
    Status read_signed(std::int64_t& out_value) override;

    Status read_unsigned(ContextId context, std::uint64_t& out_value);
    Status read_signed(ContextId context, std::int64_t& out_value) override;
    Status read_unsigned(std::size_t context_bank, ContextId context, std::uint64_t& out_value);
    Status read_signed(std::size_t context_bank, ContextId context, std::int64_t& out_value);
    Status begin_independent_scalar_contexts(std::size_t scalar_context_count) override;
    Status end_independent_scalar_contexts() override;
    Status set_state_transition(const syntax::StateTransitionTable& state_transition) override;
    Status set_legacy_v0_arithmetic(bool enabled);

private:
    struct ScalarContextSnapshot {
        std::vector<std::size_t> bank_offsets;
        std::vector<std::size_t> bank_sizes;
        std::vector<ScalarContextStates> contexts;
    };

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
    bool legacy_v0_arithmetic_ = false;
    syntax::StateTransitionTable state_transition_ = syntax::kDefaultStateTransition;
    std::vector<std::size_t> scalar_context_bank_offsets_;
    std::vector<std::size_t> scalar_context_bank_sizes_;
    std::vector<ScalarContextStates> scalar_contexts_;
    std::vector<ScalarContextSnapshot> scalar_context_snapshots_;
};

} // namespace mffv1::entropy
