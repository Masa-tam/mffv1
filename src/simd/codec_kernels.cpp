#include "simd/codec_kernels.hpp"

#include "simd/cpu_features.hpp"

namespace mffv1::simd {

CodecKernels make_codec_kernels(const CpuFeatures& requested) noexcept
{
    CodecKernels kernels;
    kernels.available_features = resolve_cpu_features(requested);
    return kernels;
}

} // namespace mffv1::simd
