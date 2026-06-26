#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <vector>

#include "codec/slice_state.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/stream_parameters.hpp"
#include "simd/codec_kernels.hpp"

namespace mffv1::codec {

class SliceEncodeExecutor {
public:
    explicit SliceEncodeExecutor(
        const syntax::StreamParameters& stream) noexcept;
    SliceEncodeExecutor(const syntax::StreamParameters& stream,
                        int thread_count,
                        CpuFeatures cpu = {}) noexcept;
    SliceEncodeExecutor(const syntax::StreamParameters& stream,
                        int thread_count,
                        const simd::CodecKernels& kernels) noexcept;

    Status encode(FrameView input,
                  std::vector<std::byte>& out_frame);
    Status encode(FrameView input,
                  bool keyframe,
                  std::vector<std::byte>& out_frame);

    [[nodiscard]] bool has_reference_state() const noexcept;
    [[nodiscard]] std::uint32_t thread_count() const noexcept;
    [[nodiscard]] std::size_t worker_count_for(
        std::size_t slice_count) const noexcept;

private:
    using SliceLayout = std::array<std::uint32_t, 4>;

    Status encode_slice(FrameView input,
                        std::size_t slice_index,
                        bool keyframe,
                        SliceState& state,
                        std::vector<std::byte>& out_slice) const;
    Status append_slices(
        const std::vector<std::vector<std::byte>>& slices,
        std::vector<std::byte>& out_frame) const;
    [[nodiscard]] SliceLayout slice_layout(std::size_t slice_index) const noexcept;

    const syntax::StreamParameters& stream_;
    std::uint32_t thread_count_ = 1;
    simd::CodecKernels kernels_;
    std::vector<SliceState> slice_states_;
    std::vector<SliceLayout> slice_layouts_;
};

} // namespace mffv1::codec
