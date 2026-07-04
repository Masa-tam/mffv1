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
class SliceInputWindow;

} // namespace mffv1::codec

namespace mffv1::entropy {

class RangeEncoder;

} // namespace mffv1::entropy

namespace mffv1::codec {

class SliceState {
public:
    Status reset(const syntax::StreamParameters& stream);
    Status reset(const SliceInputWindow& input);
    Status reset(const syntax::StreamParameters& stream, const SliceInputWindow& input);
    Status reset(const SliceOutputWindow& output);
    Status reset(const syntax::StreamParameters& stream, const SliceOutputWindow& output);
    Status prepare_golomb_rice(std::span<const std::size_t> context_counts);
    Status prepare_golomb_rice(std::span<const std::size_t> context_counts,
                               std::size_t run_state_count);
    Status capture_range_contexts(const entropy::RangeCoder& reader);
    Status capture_range_contexts(const entropy::RangeEncoder& writer);
    void clear_range_contexts() noexcept;
    void clear_entropy_state() noexcept;

    [[nodiscard]] std::size_t plane_count() const noexcept;
    [[nodiscard]] bool has_range_contexts() const noexcept;
    [[nodiscard]] bool has_golomb_rice_state() const noexcept;
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
