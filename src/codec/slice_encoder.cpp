#include "codec/slice_encoder.hpp"

#include "codec/frame_validator.hpp"
#include "codec/slice_footer_writer.hpp"
#include "codec/slice_header_writer.hpp"
#include "entropy/golomb_rice_context.hpp"
#include "entropy/golomb_rice_run.hpp"
#include "entropy/golomb_rice_writer.hpp"
#include "entropy/range_encoder.hpp"
#include "mffv1/color_transform.hpp"
#include "mffv1/context_model.hpp"
#include "mffv1/line_state.hpp"
#include "mffv1/predictor.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace mffv1::codec {

namespace {

const simd::CodecKernels& scalar_kernels() noexcept
{
    static const simd::CodecKernels kernels;
    return kernels;
}

} // namespace

SliceEncoder::SliceEncoder(const syntax::StreamParameters& stream) noexcept
    : SliceEncoder(stream, scalar_kernels())
{
}

SliceEncoder::SliceEncoder(
    const syntax::StreamParameters& stream,
    const simd::CodecKernels& kernels) noexcept
    : stream_(stream)
    , kernels_(kernels)
{
}

Status SliceEncoder::validate_stream() const
{
    if ((stream_.entropy_mode != EntropyMode::Range
         && stream_.entropy_mode != EntropyMode::GolombRice)
        || (stream_.colorspace_type != 0
            && stream_.colorspace_type != 1)
        || stream_.bits_per_raw_sample < 8
        || stream_.bits_per_raw_sample > 16) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice encoder supports only range or Golomb-Rice coding");
    }
    if (stream_.entropy_mode == EntropyMode::GolombRice
        && (stream_.bits_per_raw_sample < 8
            || stream_.bits_per_raw_sample > 16)) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "Golomb-Rice slice encoding supports only 8-16 bit streams");
    }
    if (stream_.colorspace_type == 1
        && (!stream_.chroma_planes
            || stream_.log2_h_chroma_subsample != 0
            || stream_.log2_v_chroma_subsample != 0)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "RGB streams require three full-resolution color planes");
    }
    if (!stream_.chroma_planes
        && (stream_.log2_h_chroma_subsample != 0
            || stream_.log2_v_chroma_subsample != 0)) {
        return make_error(
            ErrorCode::InvalidArgument,
            "chroma subsampling requires chroma planes");
    }
    if (stream_.log2_h_chroma_subsample > 1
        || stream_.log2_v_chroma_subsample > 1
        || stream_.log2_v_chroma_subsample
            > stream_.log2_h_chroma_subsample) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice encoder supports only 4:4:4, 4:2:2, and 4:2:0 chroma geometry");
    }
    if (stream_.quant_table_sets.size() != 1) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice encoder requires exactly one quantization table set");
    }
    if (!stream_.initial_states.empty()
        && stream_.initial_states.size() != stream_.quant_table_sets.size()) {
        return make_error(
            ErrorCode::InvalidState,
            "range coder initial state set count does not match quantization table set count");
    }
    const auto context_count = stream_.quant_table_sets[0].context_count;
    if (context_count == 0) {
        return make_error(
            ErrorCode::InvalidState,
            "slice encoder quantization table set has no contexts");
    }
    if (!stream_.initial_states.empty()
        && !stream_.initial_states[0].contexts.empty()
        && stream_.initial_states[0].contexts.size() != context_count) {
        return make_error(
            ErrorCode::InvalidState,
            "range coder initial state count does not match quantization contexts");
    }
    return ok_status();
}

Status SliceEncoder::encode_content(
    FrameView input,
    std::vector<std::byte>& out_payload) const
{
    Status status = validate_stream();
    if (!status.ok()) {
        return status;
    }

    const FrameValidator validator;
    status = validator.validate_input(stream_, input);
    if (!status.ok()) {
        return status;
    }

    SliceHeaderValues header;
    header.width = stream_.num_h_slices;
    header.height = stream_.num_v_slices;
    header.quant_table_set_indexes.assign(
        syntax::quant_table_set_index_count(stream_), 0);
    syntax::SliceDescriptor descriptor;
    const SliceHeaderParser header_parser;
    status = header_parser.apply_raster(stream_, header, descriptor);
    if (!status.ok()) {
        return status;
    }
    SliceInputWindow window;
    status = window.validate(stream_, input, descriptor);
    if (!status.ok()) {
        return status;
    }

    if (stream_.entropy_mode == EntropyMode::GolombRice) {
        bitstream::BitWriter bits;
        status = encode_golomb_rice_samples(window, bits);
        if (!status.ok()) {
            return status;
        }
        status = bits.byte_align_zero();
        if (!status.ok()) {
            return status;
        }
        std::vector<std::byte> payload;
        status = bits.finalize(payload);
        if (!status.ok()) {
            return status;
        }
        out_payload = std::move(payload);
        return ok_status();
    }

    entropy::RangeEncoder writer;
    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream_));
    const auto context_count =
        static_cast<std::size_t>(stream_.quant_table_sets[0].context_count);
    std::vector<std::size_t> context_counts(plane_count, context_count);
    std::vector<std::span<const entropy::RangeEncoder::ScalarContextStates>>
        initial_state_banks(plane_count);
    if (!stream_.initial_states.empty()) {
        for (auto& bank : initial_state_banks) {
            bank = stream_.initial_states[0].contexts;
        }
    }

    status = writer.reset(
        context_counts,
        initial_state_banks,
        stream_.state_transition);
    if (!status.ok()) {
        return status;
    }

    status = encode_samples(window, writer);
    if (!status.ok()) {
        return status;
    }

    std::vector<std::byte> payload;
    status = writer.finalize(payload);
    if (!status.ok()) {
        return status;
    }
    out_payload = std::move(payload);
    return ok_status();
}

Status SliceEncoder::encode_slice(
    FrameView input,
    bool keyframe,
    std::vector<std::byte>& out_payload) const
{
    SliceHeaderValues header;
    header.width = stream_.num_h_slices;
    header.height = stream_.num_v_slices;
    header.quant_table_set_indexes.assign(
        syntax::quant_table_set_index_count(stream_), 0);
    return encode_slice(input, header, true, keyframe, out_payload);
}

Status SliceEncoder::encode_slice(
    FrameView input,
    const SliceHeaderValues& header,
    bool write_keyframe,
    bool keyframe,
    std::vector<std::byte>& out_payload) const
{
    Status status = validate_stream();
    if (!status.ok()) {
        return status;
    }
    if (stream_.version != 3) {
        return make_error(
            ErrorCode::UnsupportedFeature,
            "slice assembly supports only FFV1 version 3");
    }
    if (write_keyframe && !keyframe && stream_.intra_only) {
        return make_error(
            ErrorCode::InvalidArgument,
            "non-keyframe is invalid for an intra-only stream");
    }

    const FrameValidator validator;
    status = validator.validate_input(stream_, input);
    if (!status.ok()) {
        return status;
    }

    syntax::SliceDescriptor descriptor;
    const SliceHeaderParser header_parser;
    status = header_parser.apply_raster(stream_, header, descriptor);
    if (!status.ok()) {
        return status;
    }
    SliceInputWindow window;
    status = window.validate(stream_, input, descriptor);
    if (!status.ok()) {
        return status;
    }

    entropy::RangeEncoder writer;
    status = writer.reset(stream_.state_transition);
    if (!status.ok()) {
        return status;
    }
    if (write_keyframe) {
        status = writer.write_bool(keyframe);
        if (!status.ok()) {
            return status;
        }
    }

    const SliceHeaderWriter header_writer;
    status = header_writer.write(writer, stream_, header);
    if (!status.ok()) {
        return status;
    }

    if (stream_.entropy_mode == EntropyMode::GolombRice) {
        std::vector<std::byte> payload;
        status = writer.finalize(payload);
        if (!status.ok()) {
            return status;
        }
        bitstream::BitWriter content_writer;
        status = encode_golomb_rice_samples(window, content_writer);
        if (!status.ok()) {
            return status;
        }
        status = content_writer.byte_align_zero();
        if (!status.ok()) {
            return status;
        }
        std::vector<std::byte> content;
        status = content_writer.finalize(content);
        if (!status.ok()) {
            return status;
        }
        if (content.size() > payload.max_size() - payload.size()) {
            return make_error(
                ErrorCode::ResourceExhausted,
                "Golomb-Rice slice payload exceeds vector capacity");
        }
        payload.insert(payload.end(), content.begin(), content.end());
        const SliceFooterWriter footer_writer;
        status = footer_writer.append(stream_, 0, payload);
        if (!status.ok()) {
            return status;
        }
        out_payload = std::move(payload);
        return ok_status();
    }

    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream_));
    const auto context_count =
        static_cast<std::size_t>(stream_.quant_table_sets[0].context_count);
    std::vector<std::size_t> context_counts(plane_count, context_count);
    std::vector<std::span<const entropy::RangeEncoder::ScalarContextStates>>
        initial_state_banks(plane_count);
    if (!stream_.initial_states.empty()) {
        for (auto& bank : initial_state_banks) {
            bank = stream_.initial_states[0].contexts;
        }
    }
    status = writer.reconfigure_contexts(
        context_counts, initial_state_banks);
    if (!status.ok()) {
        return status;
    }
    status = encode_samples(window, writer);
    if (!status.ok()) {
        return status;
    }

    std::vector<std::byte> payload;
    status = writer.finalize(payload);
    if (!status.ok()) {
        return status;
    }
    const SliceFooterWriter footer_writer;
    status = footer_writer.append(stream_, 0, payload);
    if (!status.ok()) {
        return status;
    }
    out_payload = std::move(payload);
    return ok_status();
}

Status SliceEncoder::encode_samples(
    const SliceInputWindow& input,
    entropy::RangeEncoder& writer) const
{
    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream_));
    std::vector<syntax::LineState> lines(plane_count);
    for (std::size_t plane_index = 0;
         plane_index < plane_count;
         ++plane_index) {
        Status status = lines[plane_index].reset(
            input.plane_width(plane_index));
        if (!status.ok()) {
            return status;
        }
    }
    const syntax::ContextModel context_model(stream_.quant_table_sets[0]);
    const bool use_signed_16bit_prediction =
        syntax::uses_signed_16bit_predictor(stream_);
    const std::uint32_t maximum_sample =
        stream_.bits_per_raw_sample == 16
        ? 0xffffu
        : (std::uint32_t{1} << stream_.bits_per_raw_sample) - 1u;

    const auto read_sample = [&](std::size_t plane_index,
                                 std::uint32_t x,
                                 std::uint32_t y,
                                 std::uint32_t& sample) -> Status {
        if (stream_.bits_per_raw_sample <= 8) {
            const auto* row = input.row_u8(plane_index, y);
            sample = row[x];
        } else {
            const auto* row = input.row_u16(plane_index, y);
            std::uint16_t wide_sample = 0;
            std::memcpy(
                &wide_sample,
                row + x,
                sizeof(wide_sample));
            sample = wide_sample;
        }
        if (sample > maximum_sample) {
            return make_error(
                ErrorCode::InvalidArgument,
                "input sample exceeds configured bit depth");
        }
        return ok_status();
    };

    const auto encode_sample = [&](std::size_t plane_index,
                                   std::int32_t sample,
                                   std::uint8_t reconstruction_bits,
                                   bool signed_16bit_prediction,
                                   syntax::LineState& line,
                                   std::uint32_t x) -> Status {
        const auto neighbors = line.neighbors(x);
        const auto prediction = signed_16bit_prediction
            ? syntax::Predictor::median_predict_signed_16bit(
                neighbors.left,
                neighbors.top,
                neighbors.top_left)
            : syntax::Predictor::median_predict(
                neighbors.left,
                neighbors.top,
                neighbors.top_left);
        syntax::ContextDecision context;
        Status status = context_model.derive_context(neighbors, context);
        if (!status.ok()) {
            return status;
        }
        auto difference = syntax::Predictor::difference(
            sample, prediction, reconstruction_bits);
        if (context.invert_difference) {
            difference = -difference;
        }
        status = writer.write_signed(
            plane_index, context.context, difference);
        if (!status.ok()) {
            return status;
        }
        line.mutable_current()[x] = sample;
        return ok_status();
    };

    if (stream_.colorspace_type == 1) {
        const auto width = input.plane_width(0);
        const auto height = input.plane_height(0);
        const auto coded_bits =
            static_cast<std::uint8_t>(stream_.bits_per_raw_sample + 1);
        std::array<std::vector<std::int32_t>, 3> transformed;
        for (auto& component : transformed) {
            component.resize(width);
        }

        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint32_t r = 0;
                std::uint32_t g = 0;
                std::uint32_t b = 0;
                Status status = read_sample(0, x, y, r);
                if (!status.ok()) {
                    return status;
                }
                status = read_sample(1, x, y, g);
                if (!status.ok()) {
                    return status;
                }
                status = read_sample(2, x, y, b);
                if (!status.ok()) {
                    return status;
                }
                const auto code = kernels_.forward_color_transform(
                    static_cast<std::uint16_t>(r),
                    static_cast<std::uint16_t>(g),
                    static_cast<std::uint16_t>(b),
                    stream_.bits_per_raw_sample,
                    stream_.extra_plane);
                transformed[0][x] = code.y;
                transformed[1][x] = code.cb;
                transformed[2][x] = code.cr;
            }

            for (std::size_t plane_index = 0;
                 plane_index < plane_count;
                 ++plane_index) {
                auto& line = lines[plane_index];
                for (std::uint32_t x = 0; x < width; ++x) {
                    std::int32_t sample = 0;
                    std::uint8_t reconstruction_bits = coded_bits;
                    if (plane_index < 3) {
                        sample = transformed[plane_index][x];
                    } else {
                        std::uint32_t alpha = 0;
                        Status status =
                            read_sample(plane_index, x, y, alpha);
                        if (!status.ok()) {
                            return status;
                        }
                        sample = static_cast<std::int32_t>(alpha);
                        reconstruction_bits =
                            stream_.bits_per_raw_sample;
                    }
                    Status status = encode_sample(
                        plane_index,
                        sample,
                        reconstruction_bits,
                        false,
                        line,
                        x);
                    if (!status.ok()) {
                        return status;
                    }
                }
            }
            for (auto& line : lines) {
                line.swap_lines();
            }
        }
        return ok_status();
    }

    for (std::size_t plane_index = 0;
         plane_index < plane_count;
         ++plane_index) {
        auto& line = lines[plane_index];
        const auto width = input.plane_width(plane_index);
        const auto height = input.plane_height(plane_index);

        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint32_t sample = 0;
                Status status =
                    read_sample(plane_index, x, y, sample);
                if (!status.ok()) {
                    return status;
                }
                status = encode_sample(
                    plane_index,
                    static_cast<std::int32_t>(sample),
                    stream_.bits_per_raw_sample,
                    use_signed_16bit_prediction,
                    line,
                    x);
                if (!status.ok()) {
                    return status;
                }
            }
            line.swap_lines();
        }
    }
    return ok_status();
}

Status SliceEncoder::encode_golomb_rice_samples(
    const SliceInputWindow& input,
    bitstream::BitWriter& bit_writer) const
{
    const auto plane_count =
        static_cast<std::size_t>(syntax::coded_plane_count(stream_));
    std::vector<syntax::LineState> lines(plane_count);
    std::vector<std::vector<entropy::GolombRiceContextState>> contexts(
        plane_count);
    std::vector<entropy::GolombRiceRunState> run_states(plane_count);
    const syntax::ContextModel context_model(stream_.quant_table_sets[0]);
    for (std::size_t plane_index = 0;
         plane_index < plane_count;
         ++plane_index) {
        Status status = lines[plane_index].reset(
            input.plane_width(plane_index));
        if (!status.ok()) {
            return status;
        }
        contexts[plane_index].resize(context_model.context_count());
    }
    entropy::GolombRiceWriter writer(bit_writer);
    const std::uint32_t maximum_sample =
        stream_.bits_per_raw_sample == 16
        ? 0xffffu
        : (std::uint32_t{1} << stream_.bits_per_raw_sample) - 1u;

    const auto load_input_sample = [&](std::size_t plane_index,
                                       std::uint32_t x,
                                       std::uint32_t y,
                                       std::uint32_t& sample) -> Status {
        if (stream_.bits_per_raw_sample <= 8) {
            const auto* row = input.row_u8(plane_index, y);
            sample = row[x];
        } else {
            const auto* row = input.row_u16(plane_index, y);
            std::uint16_t wide_sample = 0;
            std::memcpy(
                &wide_sample,
                row + x,
                sizeof(wide_sample));
            sample = wide_sample;
        }
        if (sample > maximum_sample) {
            return make_error(
                ErrorCode::InvalidArgument,
                "input sample exceeds configured bit depth");
        }
        return ok_status();
    };

    const auto encode_line = [&](std::span<const std::int32_t> samples,
                                 std::uint8_t reconstruction_bits,
                                 syntax::LineState& line,
                                 std::vector<entropy::GolombRiceContextState>&
                                     plane_contexts,
                                 entropy::GolombRiceRunState& run_state)
        -> Status {
        const auto width = static_cast<std::uint32_t>(samples.size());
        std::uint32_t x = 0;
        while (x < width) {
            const auto neighbors = line.neighbors(x);
            const auto prediction = syntax::Predictor::median_predict(
                neighbors.left, neighbors.top, neighbors.top_left);
            syntax::ContextDecision context;
            Status status =
                context_model.derive_context(neighbors, context);
            if (!status.ok()) {
                return status;
            }

            if (context.context == 0) {
                const auto run_start = x;
                while (x < width) {
                    const auto run_neighbors = line.neighbors(x);
                    const auto run_prediction =
                        syntax::Predictor::median_predict(
                            run_neighbors.left,
                            run_neighbors.top,
                            run_neighbors.top_left);
                    if (samples[x] != run_prediction) {
                        break;
                    }
                    line.mutable_current()[x] = samples[x];
                    ++x;
                }
                status = entropy::write_golomb_rice_run(
                    bit_writer,
                    run_state,
                    run_start,
                    width,
                    x - run_start);
                if (!status.ok()) {
                    return status;
                }
                if (x == width) {
                    continue;
                }

                const auto interruption_neighbors = line.neighbors(x);
                const auto interruption_prediction =
                    syntax::Predictor::median_predict(
                        interruption_neighbors.left,
                        interruption_neighbors.top,
                        interruption_neighbors.top_left);
                const auto difference = syntax::Predictor::difference(
                    samples[x],
                    interruption_prediction,
                    reconstruction_bits);
                status = entropy::write_golomb_rice_run_interruption(
                    writer,
                    plane_contexts[0],
                    reconstruction_bits,
                    difference);
                if (!status.ok()) {
                    return status;
                }
                line.mutable_current()[x] = samples[x];
                ++x;
                continue;
            }

            auto difference = syntax::Predictor::difference(
                samples[x], prediction, reconstruction_bits);
            if (context.invert_difference) {
                difference = -difference;
            }
            status = entropy::write_golomb_rice_symbol(
                writer,
                plane_contexts[context.context],
                reconstruction_bits,
                difference);
            if (!status.ok()) {
                return status;
            }
            line.mutable_current()[x] = samples[x];
            ++x;
        }
        line.swap_lines();
        return ok_status();
    };

    if (stream_.colorspace_type == 1) {
        const auto width = input.plane_width(0);
        const auto coded_bits =
            static_cast<std::uint8_t>(stream_.bits_per_raw_sample + 1);
        std::array<std::vector<std::int32_t>, 4> rows;
        for (auto& row : rows) {
            row.resize(width);
        }

        for (std::uint32_t y = 0; y < input.plane_height(0); ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint32_t r = 0;
                std::uint32_t g = 0;
                std::uint32_t b = 0;
                Status status = load_input_sample(0, x, y, r);
                if (!status.ok()) {
                    return status;
                }
                status = load_input_sample(1, x, y, g);
                if (!status.ok()) {
                    return status;
                }
                status = load_input_sample(2, x, y, b);
                if (!status.ok()) {
                    return status;
                }
                const auto code = kernels_.forward_color_transform(
                    static_cast<std::uint16_t>(r),
                    static_cast<std::uint16_t>(g),
                    static_cast<std::uint16_t>(b),
                    stream_.bits_per_raw_sample,
                    stream_.extra_plane);
                rows[0][x] = code.y;
                rows[1][x] = code.cb;
                rows[2][x] = code.cr;
                if (stream_.extra_plane) {
                    std::uint32_t alpha = 0;
                    status = load_input_sample(3, x, y, alpha);
                    if (!status.ok()) {
                        return status;
                    }
                    rows[3][x] = static_cast<std::int32_t>(alpha);
                }
            }

            for (std::size_t plane_index = 0;
                 plane_index < plane_count;
                 ++plane_index) {
                const auto reconstruction_bits = plane_index < 3
                    ? coded_bits
                    : stream_.bits_per_raw_sample;
                Status status = encode_line(
                    rows[plane_index],
                    reconstruction_bits,
                    lines[plane_index],
                    contexts[plane_index],
                    run_states[plane_index]);
                if (!status.ok()) {
                    return status;
                }
            }
        }
        return ok_status();
    }

    for (std::size_t plane_index = 0;
         plane_index < plane_count;
         ++plane_index) {
        auto& line = lines[plane_index];
        auto& plane_contexts = contexts[plane_index];
        auto& run_state = run_states[plane_index];
        const auto width = input.plane_width(plane_index);
        const auto height = input.plane_height(plane_index);
        std::vector<std::int32_t> samples(width);

        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::uint32_t sample = 0;
                Status status =
                    load_input_sample(plane_index, x, y, sample);
                if (!status.ok()) {
                    return status;
                }
                samples[x] = static_cast<std::int32_t>(sample);
            }
            Status status = encode_line(
                samples,
                stream_.bits_per_raw_sample,
                line,
                plane_contexts,
                run_state);
            if (!status.ok()) {
                return status;
            }
        }
    }
    return ok_status();
}

} // namespace mffv1::codec
