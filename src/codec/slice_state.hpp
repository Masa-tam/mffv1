#pragma once

#include <cstddef>
#include <vector>

#include "ffv1/line_state.hpp"
#include "ffv1/result.hpp"
#include "ffv1/stream_parameters.hpp"

namespace ffv1::codec {

class SliceOutputWindow;

class SliceState {
public:
    Status reset(const syntax::StreamParameters& stream);
    Status reset(const SliceOutputWindow& output);

    [[nodiscard]] std::size_t plane_count() const noexcept;
    [[nodiscard]] syntax::LineState& line_state(std::size_t plane_index) noexcept;
    [[nodiscard]] const syntax::LineState& line_state(std::size_t plane_index) const noexcept;

private:
    std::vector<syntax::LineState> line_states_;
};

} // namespace ffv1::codec
