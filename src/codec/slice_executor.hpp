#pragma once

#include "ffv1/frame.hpp"
#include "ffv1/result.hpp"
#include "ffv1/slice_descriptor.hpp"
#include "ffv1/stream_parameters.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace ffv1::codec {

class SliceExecutor {
public:
    explicit SliceExecutor(const syntax::StreamParameters& stream) noexcept;
    SliceExecutor(const syntax::StreamParameters& stream, int thread_count) noexcept;

    Status decode(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const;
    [[nodiscard]] std::uint32_t thread_count() const noexcept;
    [[nodiscard]] std::size_t worker_count_for(std::size_t slice_count) const noexcept;

private:
    Status decode_serial(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const;
    Status decode_parallel(MutableFrameView output, std::span<const syntax::SliceDescriptor> slices) const;
    Status decode_slice(MutableFrameView output, const syntax::SliceDescriptor& slice) const;

    const syntax::StreamParameters& stream_;
    std::uint32_t thread_count_ = 1;
};

} // namespace ffv1::codec
