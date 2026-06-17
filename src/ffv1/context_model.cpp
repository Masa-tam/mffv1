#include "ffv1/context_model.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace ffv1::syntax {

namespace {

std::uint8_t context_input_index(std::int32_t value) noexcept
{
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(value) & 0xffu);
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
                                    std::uint32_t& out_context) const
{
    if (quant_tables_.context_count == 0) {
        return make_error(ErrorCode::InvalidState, "quantization table set has no contexts");
    }

    const std::array<std::int32_t, QuantTableSet::kContextInputs> gradients{
        samples.left - samples.top_left,
        samples.top_left - samples.top,
        samples.top - samples.top_right,
        samples.left - samples.top,
        samples.top_left - samples.top_right,
    };

    std::int64_t folded = 0;
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        const auto table_index = context_input_index(gradients[i]);
        folded += quant_tables_.tables[i][table_index];
    }

    if (folded < 0) {
        folded = -folded;
    }
    out_context = static_cast<std::uint32_t>(folded % quant_tables_.context_count);
    return ok_status();
}

} // namespace ffv1::syntax

