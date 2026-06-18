#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "entropy/golomb_rice_context.hpp"
#include "entropy/golomb_rice_run.hpp"
#include "ffv1/line_state.hpp"
#include "ffv1/result.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class SliceOutputWindow;

class SliceState {
public:
    Status reset(const syntax::StreamParameters& stream);
    Status reset(const SliceOutputWindow& output);
    Status reset_golomb_rice(std::span<const std::size_t> context_counts);

    [[nodiscard]] std::size_t plane_count() const noexcept;
    [[nodiscard]] syntax::LineState& line_state(std::size_t plane_index) noexcept;
    [[nodiscard]] const syntax::LineState& line_state(std::size_t plane_index) const noexcept;
    [[nodiscard]] entropy::GolombRiceContextState& golomb_rice_context(
        std::size_t plane_index,
        std::size_t context) noexcept;
    [[nodiscard]] entropy::GolombRiceRunState& golomb_rice_run_state(
        std::size_t plane_index) noexcept;

private:
    std::vector<syntax::LineState> line_states_;
    std::vector<std::vector<entropy::GolombRiceContextState>> golomb_rice_contexts_;
    std::vector<entropy::GolombRiceRunState> golomb_rice_run_states_;
};

} // namespace ffv1::codec
