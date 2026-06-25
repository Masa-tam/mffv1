#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "entropy/symbol_writer.hpp"
#include "mffv1/state_transition.hpp"

namespace mffv1::entropy {

class RangeEncoder final : public SymbolWriter {
public:
    static constexpr std::uint8_t kDefaultInitialState = 128;

    Status reset(
        const syntax::StateTransitionTable& state_transition =
            syntax::kDefaultStateTransition);

    Status write_bool(bool value) override;
    Status write_unsigned(std::uint64_t value) override;
    Status write_signed(std::int64_t value) override;

    Status finalize(std::vector<std::byte>& out_bytes);

    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] bool finalized() const noexcept;
    [[nodiscard]] std::size_t byte_count() const noexcept;

private:
    Status add_to_low(std::uint32_t value) noexcept;

    std::vector<std::byte> low_bytes_;
    std::uint32_t range_ = 0;
    std::uint8_t bool_state_ = kDefaultInitialState;
    syntax::StateTransitionTable state_transition_ = syntax::kDefaultStateTransition;
    bool initialized_ = false;
    bool finalized_ = false;
};

} // namespace mffv1::entropy
