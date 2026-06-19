#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "entropy/golomb_rice_context.hpp"
#include "entropy/golomb_rice_run.hpp"
#include "entropy/range_coder.hpp"
#include "mffv1/line_state.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class SliceOutputWindow;

class SliceState {
public:
    Status reset(const syntax::StreamParameters& stream);
    Status reset(const SliceOutputWindow& output);
    Status reset_golomb_rice(std::span<const std::size_t> context_counts);
    Status capture_range_contexts(const entropy::RangeCoder& reader);
    void clear_range_contexts() noexcept;

    [[nodiscard]] std::size_t plane_count() const noexcept;
    [[nodiscard]] bool has_range_contexts() const noexcept;
    [[nodiscard]] const entropy::RangeCoder::ContextStateBanks& range_contexts() const noexcept;
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
    entropy::RangeCoder::ContextStateBanks range_contexts_;
};

} // namespace mffv1::codec
