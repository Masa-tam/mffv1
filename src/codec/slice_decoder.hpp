#pragma once

#include "codec/slice_output_window.hpp"
#include "codec/slice_state.hpp"
#include "entropy/golomb_rice_context.hpp"
#include "entropy/golomb_rice_run.hpp"
#include "mffv1/context_model.hpp"
#include "mffv1/result.hpp"
#include "mffv1/slice_descriptor.hpp"
#include "mffv1/stream_parameters.hpp"
#include "simd/codec_kernels.hpp"

#include <cstddef>
#include <cstdint>

namespace mffv1::codec {

struct GolombRiceSampleTrace {
    std::size_t plane = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::size_t context_bank = 0;
    syntax::ContextDecision context{};
    syntax::NeighborSamples neighbors{};
    entropy::GolombRiceRunState run_state_before{};
    entropy::GolombRiceRunState run_state_after{};
    entropy::GolombRiceContextState adaptive_state_before{};
    entropy::GolombRiceContextState adaptive_state_after{};
    std::uint64_t bit_position_before = 0;
    std::uint64_t bit_position_after = 0;
    std::int32_t prediction = 0;
    std::int32_t difference = 0;
    std::int32_t reconstructed_sample = 0;
    bool run_interruption = false;
};

class SliceDecodeObserver {
public:
    virtual ~SliceDecodeObserver() = default;
    virtual void on_golomb_rice_sample(const GolombRiceSampleTrace& trace) = 0;
};

class SliceDecoder {
public:
    explicit SliceDecoder(const syntax::StreamParameters& stream) noexcept;
    SliceDecoder(const syntax::StreamParameters& stream,
                 bool allow_golomb_rice_read_ahead_trailing_bytes) noexcept;
    SliceDecoder(const syntax::StreamParameters& stream,
                 const simd::CodecKernels& kernels) noexcept;
    SliceDecoder(const syntax::StreamParameters& stream,
                 const simd::CodecKernels& kernels,
                 bool allow_golomb_rice_read_ahead_trailing_bytes) noexcept;

    Status validate(const syntax::SliceDescriptor& slice,
                    const SliceOutputWindow& output) const;
    Status decode(const syntax::SliceDescriptor& slice,
                  SliceOutputWindow& output,
                  SliceState& state) const;
    Status decode(const syntax::SliceDescriptor& slice,
                  SliceOutputWindow& output,
                  SliceState& state,
                  SliceDecodeObserver* observer) const;

private:
    Status resolve_content_payload(const syntax::SliceDescriptor& slice,
                                   ByteSpan& out_payload) const;

    const syntax::StreamParameters& stream_;
    const simd::CodecKernels& kernels_;
    bool allow_golomb_rice_read_ahead_trailing_bytes_ = false;
};

} // namespace mffv1::codec
