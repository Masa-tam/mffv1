#pragma once

#include <cstdint>
#include <vector>

#include "ffv1/context_model.hpp"
#include "ffv1/result.hpp"

namespace ffv1::syntax {

class LineState {
public:
    Status reset(std::uint32_t width);

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] const std::vector<std::int32_t>& second_previous() const noexcept;
    [[nodiscard]] const std::vector<std::int32_t>& previous() const noexcept;
    [[nodiscard]] const std::vector<std::int32_t>& current() const noexcept;
    [[nodiscard]] std::vector<std::int32_t>& mutable_previous() noexcept;
    [[nodiscard]] std::vector<std::int32_t>& mutable_current() noexcept;
    [[nodiscard]] NeighborSamples neighbors(std::uint32_t x) const noexcept;

    void swap_lines() noexcept;

private:
    std::vector<std::int32_t> second_previous_;
    std::vector<std::int32_t> previous_;
    std::vector<std::int32_t> current_;
};

} // namespace ffv1::syntax
