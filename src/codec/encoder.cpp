#include "mffv1/codec.hpp"

#include "codec/configuration_record_writer.hpp"
#include "codec/encoder_profile.hpp"
#include "codec/slice_encode_executor.hpp"
#include "simd/codec_kernels.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace mffv1 {

namespace {

class Encoder final : public IEncoder {
public:
    explicit Encoder(EncoderOptions options)
        : options_(options)
        , kernels_(simd::make_codec_kernels(options.cpu))
    {
    }

    Status configure(const StreamInfo& stream, ConfigurationRecord& out_record) override
    {
        syntax::StreamParameters normalized;
        Status status = codec::normalize_encoder_profile(
            options_, stream, normalized);
        if (!status.ok()) {
            return status;
        }

        std::vector<std::byte> record_bytes;
        const codec::ConfigurationRecordWriter writer;
        status = writer.write(normalized, record_bytes);
        if (!status.ok()) {
            return status;
        }

        stream_ = std::move(normalized);
        // SliceEncodeExecutor borrows stream_; rebuild it immediately after
        // stream_ is assigned so the reference follows this Encoder instance.
        executor_ = std::make_unique<codec::SliceEncodeExecutor>(
            *stream_, options_.thread_count, kernels_);
        next_frame_index_ = 0;
        out_record.bytes = std::move(record_bytes);
        return ok_status();
    }

    Status encode_frame(FrameView input, EncodedFrame& out_frame) override
    {
        if (!stream_.has_value()) {
            return make_error(ErrorCode::InvalidState, "encoder is not configured");
        }
        if (!executor_) {
            return make_error(ErrorCode::InvalidState, "encoder executor is not configured");
        }
        std::vector<std::byte> frame_bytes;
        const bool keyframe =
            next_frame_index_ % options_.keyframe_interval == 0;
        Status status = executor_->encode(input, keyframe, frame_bytes);
        if (!status.ok()) {
            return status;
        }
        out_frame.bytes = std::move(frame_bytes);
        ++next_frame_index_;
        return ok_status();
    }

private:
    EncoderOptions options_;
    simd::CodecKernels kernels_;
    std::optional<syntax::StreamParameters> stream_;
    std::unique_ptr<codec::SliceEncodeExecutor> executor_;
    std::uint64_t next_frame_index_ = 0;
};

} // namespace

EncoderFactoryResult create_encoder(const EncoderOptions& options)
{
    EncoderFactoryResult result;
    if (options.thread_count < 0) {
        result.status = make_error(
            ErrorCode::InvalidArgument,
            "encoder thread count must not be negative");
        return result;
    }
    if (options.keyframe_interval == 0) {
        result.status = make_error(
            ErrorCode::InvalidArgument,
            "encoder keyframe interval must be non-zero");
        return result;
    }
    result.status = ok_status();
    result.encoder = std::make_unique<Encoder>(options);
    return result;
}

} // namespace mffv1
