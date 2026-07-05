#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "entropy/symbol_reader.hpp"
#include "entropy/symbol_writer.hpp"
#include "mffv1/state_transition.hpp"

namespace mffv1::entropy {

class RangeEncoder final : public SymbolWriter {
public:
    static constexpr std::size_t kScalarContextSize = 32;
    using ScalarContextStates = std::array<std::uint8_t, kScalarContextSize>;
    using ContextStateBank = std::vector<ScalarContextStates>;
    using ContextStateBanks = std::vector<ContextStateBank>;
    static constexpr std::size_t kMaxScalarContextCount = 32768;
    static constexpr std::size_t kMaxContextBankCount = 4;
    static constexpr std::uint8_t kDefaultInitialState = 128;

    Status reset(
        const syntax::StateTransitionTable& state_transition =
            syntax::kDefaultStateTransition);
    Status reset(std::size_t scalar_context_count,
                 std::uint8_t initial_state = kDefaultInitialState);
    Status reset(std::span<const std::size_t> scalar_context_counts,
                 std::uint8_t initial_state = kDefaultInitialState);
    Status reset(
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks,
        const syntax::StateTransitionTable& state_transition =
            syntax::kDefaultStateTransition);
    Status reconfigure_contexts(
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks = {});
    Status copy_contexts(ContextStateBanks& out_context_banks) const;

    Status write_bool(bool value) override;
    Status write_termination_sentinel();
    Status write_unsigned(std::uint64_t value) override;
    Status write_signed(std::int64_t value) override;
    Status write_unsigned(ContextId context, std::uint64_t value) override;
    Status write_signed(ContextId context, std::int64_t value) override;
    Status write_unsigned(std::size_t context_bank,
                          ContextId context,
                          std::uint64_t value);
    Status write_signed(std::size_t context_bank,
                        ContextId context,
                        std::int64_t value);
    Status begin_independent_scalar_contexts(std::size_t scalar_context_count) override;
    Status end_independent_scalar_contexts() override;

    Status finalize(std::vector<std::byte>& out_bytes);

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] std::size_t byte_count() const noexcept;

private:
    struct ScalarContextSnapshot {
        std::vector<std::size_t> bank_offsets;
        std::vector<std::size_t> bank_sizes;
        std::vector<ScalarContextStates> contexts;
    };

    Status reset_impl(
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks,
        std::uint8_t initial_state,
        const syntax::StateTransitionTable& state_transition);
    Status initialize_contexts(
        std::span<const std::size_t> scalar_context_counts,
        std::span<const std::span<const ScalarContextStates>> initial_state_banks,
        std::uint8_t initial_state);
    Status write_rac(std::uint8_t& state, bool value);
    Status write_symbol(std::size_t context_bank,
                        ContextId context,
                        bool is_signed,
                        std::uint64_t magnitude,
                        bool negative);
    Status add_to_low(std::uint32_t value) noexcept;

    std::vector<std::byte> low_bytes_;
    std::uint32_t range_ = 0;
    syntax::StateTransitionTable state_transition_ = syntax::kDefaultStateTransition;
    std::vector<std::size_t> scalar_context_bank_offsets_;
    std::vector<std::size_t> scalar_context_bank_sizes_;
    std::vector<ScalarContextStates> scalar_contexts_;
    std::vector<ScalarContextSnapshot> scalar_context_snapshots_;
    bool initialized_ = false;
    bool finalized_ = false;
};

} // namespace mffv1::entropy
