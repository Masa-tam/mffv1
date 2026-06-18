#include "ffv1/context_model.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ffv1::syntax {

namespace {

std::uint8_t context_input_index(std::int64_t value) noexcept
{
    return static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) & 0xffu);
}

} // namespace

ContextModel::ContextModel(const QuantTableSet& quant_tables) noexcept
    : quant_tables_(quant_tables)
{
}

std::uint32_t ContextModel::context_count() const noexcept
{
    return quant_tables_.context_count;
}

Status ContextModel::derive_context(const NeighborSamples& samples,
                                    ContextDecision& out_decision) const
{
    if (quant_tables_.context_count == 0) {
        return make_error(ErrorCode::InvalidState, "quantization table set has no contexts");
    }

    const std::array<std::int64_t, QuantTableSet::kContextInputs> gradients{
        static_cast<std::int64_t>(samples.left) - samples.top_left,
        static_cast<std::int64_t>(samples.top_left) - samples.top,
        static_cast<std::int64_t>(samples.top) - samples.top_right,
        static_cast<std::int64_t>(samples.far_left) - samples.left,
        static_cast<std::int64_t>(samples.top_top) - samples.top,
    };

    std::int64_t folded = 0;
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        const auto table_index = context_input_index(gradients[i]);
        folded += quant_tables_.tables[i][table_index];
    }

    out_decision.invert_difference = folded < 0;
    if (out_decision.invert_difference) {
        folded = -folded;
    }
    if (folded >= quant_tables_.context_count) {
        return make_error(ErrorCode::InvalidState,
                          "quantized context is outside the configured context range");
    }
    out_decision.context = static_cast<std::uint32_t>(folded);
    return ok_status();
}

} // namespace ffv1::syntax
