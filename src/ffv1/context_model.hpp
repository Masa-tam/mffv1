#pragma once

#include <cstdint>

#include "ffv1/result.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::syntax {

struct NeighborSamples {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t top_left = 0;
    std::int32_t top_right = 0;
};

class ContextModel {
public:
    explicit ContextModel(const QuantTableSet& quant_tables) noexcept;

    [[nodiscard]] std::uint32_t context_count() const noexcept;
    Status derive_context(const NeighborSamples& samples, std::uint32_t& out_context) const;

private:
    const QuantTableSet& quant_tables_;
};

} // namespace ffv1::syntax

