#pragma once

#include "codec/slice_state.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"
#include "mffv1/slice_descriptor.hpp"
#include "mffv1/stream_parameters.hpp"
#include "simd/codec_kernels.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mffv1::codec {

class SliceExecutor {
public:
    explicit SliceExecutor(const syntax::StreamParameters& stream) noexcept;
    SliceExecutor(const syntax::StreamParameters& stream, int thread_count) noexcept;
    SliceExecutor(const syntax::StreamParameters& stream,
                  int thread_count,
                  const CpuFeatures& cpu) noexcept;

    Status decode(MutableFrameView output,
                  std::span<const syntax::SliceDescriptor> slices,
                  bool keyframe = true);
    [[nodiscard]] bool has_reference_state() const noexcept;
    [[nodiscard]] std::uint32_t thread_count() const noexcept;
    [[nodiscard]] std::size_t worker_count_for(std::size_t slice_count) const noexcept;

private:
    using SliceLayout = std::array<std::uint32_t, 4>;

    Status validate_slices(MutableFrameView output,
                           std::span<const syntax::SliceDescriptor> slices) const;
    Status decode_serial(MutableFrameView output,
                         std::span<const syntax::SliceDescriptor> slices,
                         std::vector<SliceState>& states) const;
    Status decode_parallel(MutableFrameView output,
                           std::span<const syntax::SliceDescriptor> slices,
                           std::vector<SliceState>& states) const;
    Status decode_slice(MutableFrameView output,
                        const syntax::SliceDescriptor& slice,
                        SliceState& state) const;

    const syntax::StreamParameters& stream_;
    simd::CodecKernels kernels_;
    std::uint32_t thread_count_ = 1;
    std::vector<SliceState> slice_states_;
    std::vector<SliceLayout> slice_layouts_;
};

} // namespace mffv1::codec
