#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"

namespace mffv1::codec {

class SliceEncodeExecutor {
public:
    explicit SliceEncodeExecutor(
        const syntax::StreamParameters& stream) noexcept;
    SliceEncodeExecutor(const syntax::StreamParameters& stream,
                        int thread_count) noexcept;

    Status encode(FrameView input,
                  std::vector<std::byte>& out_frame) const;

    [[nodiscard]] std::uint32_t thread_count() const noexcept;
    [[nodiscard]] std::size_t worker_count_for(
        std::size_t slice_count) const noexcept;

private:
    Status encode_slice(FrameView input,
                        std::size_t slice_index,
                        std::vector<std::byte>& out_slice) const;
    Status append_slices(
        const std::vector<std::vector<std::byte>>& slices,
        std::vector<std::byte>& out_frame) const;

    const syntax::StreamParameters& stream_;
    std::uint32_t thread_count_ = 1;
};

} // namespace mffv1::codec
