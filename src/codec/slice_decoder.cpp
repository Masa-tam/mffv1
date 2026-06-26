#include "codec/slice_decoder.hpp"

#include "codec/slice_header_parser.hpp"
#include "bitstream/bit_reader.hpp"
#include "entropy/golomb_rice_context.hpp"
#include "entropy/golomb_rice_reader.hpp"
#include "entropy/golomb_rice_run.hpp"
#include "entropy/range_coder.hpp"
#include "mffv1/color_transform.hpp"
#include "mffv1/context_model.hpp"
#include "mffv1/predictor.hpp"
#include "util/status.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace mffv1::syntax {

Status LineState::reset(std::uint32_t width)
{
    if (width == 0) {
        return make_error(ErrorCode::InvalidArgument, "line state width must be non-zero");
    }
    previous_.assign(width, 0);
    second_previous_.assign(width, 0);
    current_.assign(width, 0);
    return ok_status();
}

std::uint32_t LineState::width() const noexcept
{
    return static_cast<std::uint32_t>(current_.size());
}

const std::vector<std::int32_t>& LineState::previous() const noexcept
{
    return previous_;
}

const std::vector<std::int32_t>& LineState::second_previous() const noexcept
{
    return second_previous_;
}

const std::vector<std::int32_t>& LineState::current() const noexcept
{
    return current_;
}

std::vector<std::int32_t>& LineState::mutable_previous() noexcept
{
    return previous_;
}

std::vector<std::int32_t>& LineState::mutable_current() noexcept
{
    return current_;
}

NeighborSamples LineState::neighbors(std::uint32_t x) const noexcept
{
    NeighborSamples samples;
    samples.far_left = x > 1 ? current_[x - 2] : (x == 1 ? previous_[0] : 0);
    samples.left = x > 0 ? current_[x - 1] : previous_[0];
    samples.top = previous_[x];
    samples.top_left = x > 0 ? previous_[x - 1] : second_previous_[0];
    samples.top_right = (x + 1) < current_.size() ? previous_[x + 1] : previous_[x];
    samples.top_top = second_previous_[x];
    return samples;
}

void LineState::swap_lines() noexcept
{
    second_previous_ = previous_;
    previous_.swap(current_);
    std::fill(current_.begin(), current_.end(), 0);
}

} // namespace mffv1::syntax

namespace mffv1::codec {

namespace {

const simd::CodecKernels& scalar_codec_kernels() noexcept
{
    static const simd::CodecKernels kernels;
    return kernels;
}

void add_byte_offset(Status& status, std::uint64_t base_offset) noexcept
{
    if (status.location.has_byte_offset) {
        status.location.byte_offset += base_offset;
    } else {
        set_byte_location_if_missing(status, base_offset);
    }
}

void set_reader_byte_offset(Status& status,
                            std::uint64_t content_offset,
                            std::uint64_t reader_offset) noexcept
{
    if (status.location.has_byte_offset) {
        status.location.byte_offset += content_offset;
    } else {
        set_byte_location_if_missing(status, content_offset + reader_offset);
    }
}

Status decode_range_line(entropy::RangeCoder& reader,
                         std::uint64_t reader_base_offset,
                         const syntax::ContextModel& context_model,
                         std::size_t plane,
                         std::uint32_t width,
                         std::uint8_t reconstruction_bits,
                         bool use_signed_16bit_prediction,
                         syntax::LineState& line)
{
    for (std::uint32_t x = 0; x < width; ++x) {
        const auto neighbors = line.neighbors(x);
        const std::int32_t prediction = use_signed_16bit_prediction
            ? syntax::Predictor::median_predict_signed_16bit(
                neighbors.left, neighbors.top, neighbors.top_left)
            : syntax::Predictor::median_predict(
                neighbors.left, neighbors.top, neighbors.top_left);

        syntax::ContextDecision context;
        Status status = context_model.derive_context(neighbors, context);
        if (!status.ok()) {
            return status;
        }

        std::int64_t difference64 = 0;
        status = reader.read_signed(plane, context.context, difference64);
        if (!status.ok()) {
            set_reader_byte_offset(status, reader_base_offset, reader.byte_position());
            return status;
        }
        if (context.invert_difference) {
            difference64 = -difference64;
        }
        if (difference64 < std::numeric_limits<std::int32_t>::min()
            || difference64 > std::numeric_limits<std::int32_t>::max()) {
            return make_error(ErrorCode::SyntaxError,
                              "range-coded sample difference is outside int32 range");
        }

        line.mutable_current()[x] = syntax::Predictor::reconstruct(
            prediction,
            static_cast<std::int32_t>(difference64),
            reconstruction_bits);
    }
    return ok_status();
}

Status decode_golomb_rice_line(bitstream::BitReader& bit_reader,
                               entropy::GolombRiceReader& reader,
                               std::uint64_t payload_offset,
                               const syntax::ContextModel& context_model,
                               std::size_t plane,
                               std::uint32_t width,
                               std::uint8_t reconstruction_bits,
                               SliceState& state)
{
    auto& line = state.line_state(plane);
    auto& run_state = state.golomb_rice_run_state(plane);
    std::uint32_t x = 0;
    while (x < width) {
        const auto neighbors = line.neighbors(x);
        const auto prediction = syntax::Predictor::median_predict(
            neighbors.left, neighbors.top, neighbors.top_left);
        syntax::ContextDecision context;
        Status status = context_model.derive_context(neighbors, context);
        if (!status.ok()) {
            return status;
        }

        if (context.context == 0) {
            entropy::GolombRiceRunSegment segment;
            status = entropy::read_golomb_rice_run_segment(
                bit_reader, run_state, x, width, segment);
            if (!status.ok()) {
                set_reader_byte_offset(status, payload_offset, bit_reader.byte_position());
                return status;
            }
            const auto run_end = std::min<std::uint64_t>(
                static_cast<std::uint64_t>(width),
                static_cast<std::uint64_t>(x) + segment.count);
            while (x < run_end) {
                const auto run_neighbors = line.neighbors(x);
                line.mutable_current()[x] = syntax::Predictor::median_predict(
                    run_neighbors.left, run_neighbors.top, run_neighbors.top_left);
                ++x;
            }
            if (!segment.interrupted || x == width) {
                continue;
            }

            std::int32_t difference = 0;
            status = entropy::read_golomb_rice_run_interruption(
                reader,
                state.golomb_rice_context(plane, 0),
                reconstruction_bits,
                difference);
            if (!status.ok()) {
                set_reader_byte_offset(status, payload_offset, bit_reader.byte_position());
                return status;
            }
            const auto interruption_neighbors = line.neighbors(x);
            const auto interruption_prediction = syntax::Predictor::median_predict(
                interruption_neighbors.left,
                interruption_neighbors.top,
                interruption_neighbors.top_left);
            line.mutable_current()[x] = syntax::Predictor::reconstruct(
                interruption_prediction, difference, reconstruction_bits);
            ++x;
            continue;
        }

        std::int32_t difference = 0;
        status = entropy::read_golomb_rice_symbol(
            reader,
            state.golomb_rice_context(plane, context.context),
            reconstruction_bits,
            difference);
        if (!status.ok()) {
            set_reader_byte_offset(status, payload_offset, bit_reader.byte_position());
            return status;
        }
        if (context.invert_difference) {
            difference = -difference;
        }
        line.mutable_current()[x] = syntax::Predictor::reconstruct(
            prediction, difference, reconstruction_bits);
        ++x;
    }
    return ok_status();
}

Status store_rgb_line(const syntax::StreamParameters& stream,
                      const simd::CodecKernels& kernels,
                      SliceOutputWindow& output,
                      std::uint32_t y,
                      SliceState& state,
                      std::array<std::vector<std::uint16_t>, 3>& rgb_scratch)
{
    std::array<std::uint8_t*, 4> rows_u8{};
    std::array<std::uint16_t*, 4> rows_u16{};
    for (std::size_t plane = 0; plane < output.plane_count(); ++plane) {
        rows_u8[plane] = output.row_u8(plane, y);
        rows_u16[plane] = output.row_u16(plane, y);
        if (stream.bits_per_raw_sample <= 8 && rows_u8[plane] == nullptr) {
            return make_error(ErrorCode::InvalidArgument,
                              "RGB slice output row is not writable as uint8");
        }
        if (stream.bits_per_raw_sample > 8 && rows_u16[plane] == nullptr) {
            return make_error(ErrorCode::InvalidArgument,
                              "RGB slice output row is not writable as uint16");
        }
    }

    const auto width = output.plane_width(0);
    kernels.inverse_color_transform_row(
        state.line_state(0).current().data(),
        state.line_state(1).current().data(),
        state.line_state(2).current().data(),
        rgb_scratch[0].data(),
        rgb_scratch[1].data(),
        rgb_scratch[2].data(),
        width,
        stream.bits_per_raw_sample,
        stream.extra_plane);
    for (std::uint32_t x = 0; x < width; ++x) {
        if (stream.bits_per_raw_sample <= 8) {
            rows_u8[0][x] = static_cast<std::uint8_t>(rgb_scratch[0][x]);
            rows_u8[1][x] = static_cast<std::uint8_t>(rgb_scratch[1][x]);
            rows_u8[2][x] = static_cast<std::uint8_t>(rgb_scratch[2][x]);
            if (stream.extra_plane) {
                rows_u8[3][x] = static_cast<std::uint8_t>(
                    state.line_state(3).current()[x]);
            }
        } else {
            rows_u16[0][x] = rgb_scratch[0][x];
            rows_u16[1][x] = rgb_scratch[1][x];
            rows_u16[2][x] = rgb_scratch[2][x];
            if (stream.extra_plane) {
                rows_u16[3][x] = static_cast<std::uint16_t>(
                    state.line_state(3).current()[x]);
            }
        }
    }
    for (std::size_t plane = 0; plane < output.plane_count(); ++plane) {
        state.line_state(plane).swap_lines();
    }
    return ok_status();
}

Status store_planar_line(const syntax::StreamParameters& stream,
                         SliceOutputWindow& output,
                         std::size_t plane,
                         std::uint32_t y,
                         syntax::LineState& line)
{
    auto* row_u8 = output.row_u8(plane, y);
    auto* row_u16 = output.row_u16(plane, y);
    if (stream.bits_per_raw_sample <= 8 && row_u8 == nullptr) {
        return make_error(ErrorCode::InvalidArgument,
                          "slice output row is not writable as uint8");
    }
    if (stream.bits_per_raw_sample > 8 && row_u16 == nullptr) {
        return make_error(ErrorCode::InvalidArgument,
                          "slice output row is not writable as uint16");
    }

    const auto width = output.plane_width(plane);
    for (std::uint32_t x = 0; x < width; ++x) {
        if (stream.bits_per_raw_sample <= 8) {
            row_u8[x] = static_cast<std::uint8_t>(line.current()[x]);
        } else {
            row_u16[x] = static_cast<std::uint16_t>(line.current()[x]);
        }
    }
    line.swap_lines();
    return ok_status();
}

Status decode_golomb_rice_slice(const syntax::StreamParameters& stream,
                                const simd::CodecKernels& kernels,
                                ByteSpan payload,
                                std::uint64_t payload_offset,
                                std::uint8_t content_bit_offset,
                                const std::vector<syntax::ContextModel>& context_models,
                                SliceOutputWindow& output,
                                SliceState& state)
{
    std::vector<std::size_t> context_counts;
    context_counts.reserve(context_models.size());
    for (const auto& model : context_models) {
        context_counts.push_back(model.context_count());
    }
    Status status = state.prepare_golomb_rice(context_counts);
    if (!status.ok()) {
        return status;
    }

    bitstream::BitReader bit_reader(payload);
    status = bit_reader.skip_bits(content_bit_offset);
    if (!status.ok()) {
        set_reader_byte_offset(status, payload_offset, bit_reader.byte_position());
        return status;
    }
    entropy::GolombRiceReader reader(bit_reader);
    if (stream.colorspace_type == 1) {
        const auto width = output.plane_width(0);
        const auto height = output.plane_height(0);
        std::array<std::vector<std::uint16_t>, 3> rgb_scratch;
        for (auto& plane : rgb_scratch) {
            plane.resize(width);
        }
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::size_t plane = 0; plane < output.plane_count(); ++plane) {
                const auto reconstruction_bits = plane < 3
                    ? static_cast<std::uint8_t>(stream.bits_per_raw_sample + 1)
                    : stream.bits_per_raw_sample;
                status = decode_golomb_rice_line(bit_reader,
                                                 reader,
                                                 payload_offset,
                                                 context_models[plane],
                                                 plane,
                                                 width,
                                                 reconstruction_bits,
                                                 state);
                if (!status.ok()) {
                    return status;
                }
            }
            status = store_rgb_line(
                stream, kernels, output, y, state, rgb_scratch);
            if (!status.ok()) {
                return status;
            }
        }
    } else {
        for (std::size_t plane = 0; plane < output.plane_count(); ++plane) {
            auto& line = state.line_state(plane);
            const auto width = output.plane_width(plane);
            for (std::uint32_t y = 0; y < output.plane_height(plane); ++y) {
                status = decode_golomb_rice_line(bit_reader,
                                                 reader,
                                                 payload_offset,
                                                 context_models[plane],
                                                 plane,
                                                 width,
                                                 stream.bits_per_raw_sample,
                                                 state);
                if (!status.ok()) {
                    return status;
                }
                status = store_planar_line(stream, output, plane, y, line);
                if (!status.ok()) {
                    return status;
                }
            }
        }
    }

    while ((bit_reader.bit_position() % 8) != 0) {
        const auto padding_byte_offset = bit_reader.byte_position();
        std::uint8_t padding = 0;
        status = bit_reader.read_bit(padding);
        if (!status.ok()) {
            set_reader_byte_offset(status, payload_offset, bit_reader.byte_position());
            return status;
        }
        if (padding != 0) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "Golomb-Rice alignment padding must be zero",
                                   payload_offset + padding_byte_offset);
        }
    }
    if (bit_reader.remaining_bits() != 0) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "Golomb-Rice payload contains trailing bytes",
                               payload_offset + bit_reader.byte_position());
    }
    return ok_status();
}

} // namespace

Status SliceState::reset(const syntax::StreamParameters& stream)
{
    const auto planes = syntax::coded_plane_count(stream);
    line_states_.resize(planes);
    for (std::size_t i = 0; i < line_states_.size(); ++i) {
        const std::uint32_t width = syntax::plane_width(stream, i);
        Status status = line_states_[i].reset(width);
        if (!status.ok()) {
            return status;
        }
    }
    return ok_status();
}

Status SliceState::reset(const SliceOutputWindow& output)
{
    line_states_.resize(output.plane_count());
    for (std::size_t i = 0; i < line_states_.size(); ++i) {
        Status status = line_states_[i].reset(output.plane_width(i));
        if (!status.ok()) {
            return status;
        }
    }
    return ok_status();
}

std::size_t SliceState::plane_count() const noexcept
{
    return line_states_.size();
}

Status SliceState::capture_range_contexts(const entropy::RangeCoder& reader)
{
    return reader.copy_contexts(range_contexts_);
}

void SliceState::clear_range_contexts() noexcept
{
    range_contexts_.clear();
}

bool SliceState::has_range_contexts() const noexcept
{
    return !range_contexts_.empty();
}

const entropy::RangeCoder::ContextStateBanks& SliceState::range_contexts() const noexcept
{
    return range_contexts_;
}

syntax::LineState& SliceState::line_state(std::size_t plane_index) noexcept
{
    return line_states_[plane_index];
}

const syntax::LineState& SliceState::line_state(std::size_t plane_index) const noexcept
{
    return line_states_[plane_index];
}

Status SliceState::prepare_golomb_rice(std::span<const std::size_t> context_counts)
{
    if (context_counts.size() != line_states_.size()) {
        return make_error(ErrorCode::InvalidArgument,
                          "Golomb-Rice context bank count does not match slice plane count");
    }
    for (std::size_t plane = 0; plane < context_counts.size(); ++plane) {
        if (context_counts[plane] == 0) {
            return make_error(ErrorCode::InvalidState,
                              "Golomb-Rice context bank must not be empty");
        }
    }

    if (golomb_rice_contexts_.empty() && golomb_rice_run_states_.empty()) {
        golomb_rice_contexts_.resize(context_counts.size());
        golomb_rice_run_states_.resize(context_counts.size());
        for (std::size_t plane = 0; plane < context_counts.size(); ++plane) {
            golomb_rice_contexts_[plane].resize(context_counts[plane]);
        }
        return ok_status();
    }
    if (golomb_rice_contexts_.size() != context_counts.size()
        || golomb_rice_run_states_.size() != context_counts.size()) {
        return make_error(ErrorCode::InvalidState,
                          "saved Golomb-Rice context bank count does not match slice plane count");
    }
    for (std::size_t plane = 0; plane < context_counts.size(); ++plane) {
        if (golomb_rice_contexts_[plane].size() != context_counts[plane]) {
            return make_error(ErrorCode::InvalidState,
                              "saved Golomb-Rice context count does not match quantization contexts");
        }
    }
    return ok_status();
}

bool SliceState::has_golomb_rice_state() const noexcept
{
    return !golomb_rice_contexts_.empty();
}

entropy::GolombRiceContextState& SliceState::golomb_rice_context(
    std::size_t plane_index,
    std::size_t context) noexcept
{
    return golomb_rice_contexts_[plane_index][context];
}

entropy::GolombRiceRunState& SliceState::golomb_rice_run_state(
    std::size_t plane_index) noexcept
{
    return golomb_rice_run_states_[plane_index];
}

SliceDecoder::SliceDecoder(const syntax::StreamParameters& stream) noexcept
    : SliceDecoder(stream, scalar_codec_kernels())
{
}

SliceDecoder::SliceDecoder(const syntax::StreamParameters& stream,
                           const simd::CodecKernels& kernels) noexcept
    : stream_(stream)
    , kernels_(kernels)
{
}

Status SliceDecoder::resolve_content_payload(const syntax::SliceDescriptor& slice,
                                             ByteSpan& out_payload) const
{
    if (slice.content_byte_offset < slice.payload_byte_offset) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice content offset is before payload",
                               slice.content_byte_offset);
    }
    const auto local_content_offset = slice.content_byte_offset - slice.payload_byte_offset;
    if (local_content_offset > slice.payload.size()) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice content offset is outside payload",
                               slice.content_byte_offset);
    }
    auto local_content_end = static_cast<std::uint64_t>(slice.payload.size());
    if (slice.footer_byte_offset != 0 || slice.slice_size != 0) {
        if (slice.footer_byte_offset < slice.payload_byte_offset) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice footer offset is before payload",
                                   slice.footer_byte_offset);
        }
        const auto local_footer_offset = slice.footer_byte_offset - slice.payload_byte_offset;
        if (local_footer_offset > slice.payload.size()) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice footer offset is outside payload",
                                   slice.footer_byte_offset);
        }
        if (local_footer_offset < local_content_offset) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice footer offset is before content",
                                   slice.footer_byte_offset);
        }
        local_content_end = local_footer_offset;
    }
    out_payload = slice.payload.subspan(static_cast<std::size_t>(local_content_offset),
                                        static_cast<std::size_t>(local_content_end - local_content_offset));
    const bool has_continuous_range_state = stream_.entropy_mode == EntropyMode::Range
        && ((stream_.version >= 3 && slice.slice_size != 0)
            || slice.continues_frame_range_state);
    if (out_payload.empty() && !has_continuous_range_state) {
        return make_byte_error(ErrorCode::SyntaxError, "slice payload is empty", slice.content_byte_offset);
    }
    return ok_status();
}

Status SliceDecoder::validate(const syntax::SliceDescriptor& slice,
                              const SliceOutputWindow& output) const
{
    ByteSpan payload;
    Status status = resolve_content_payload(slice, payload);
    if (!status.ok()) {
        return status;
    }
    if (slice.content_bit_offset > 7) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "slice content bit offset is outside a byte",
                               slice.content_byte_offset);
    }
    if (stream_.entropy_mode != EntropyMode::GolombRice && slice.content_bit_offset != 0) {
        return make_byte_error(ErrorCode::SyntaxError,
                               "range-coded slice content must be byte aligned",
                               slice.content_byte_offset);
    }
    if (output.plane_count() != syntax::coded_plane_count(stream_)) {
        return make_error(ErrorCode::InvalidArgument, "slice output plane count does not match stream");
    }
    if (stream_.bits_per_raw_sample == 0 || stream_.bits_per_raw_sample > 16) {
        return make_error(ErrorCode::UnsupportedFeature, "only 1-16 bit samples are supported");
    }
    if (stream_.quant_table_sets.empty()) {
        return make_error(ErrorCode::InvalidState, "stream has no quantization table sets");
    }
    if (slice.quant_table_set_indexes.empty()) {
        return make_error(ErrorCode::SyntaxError, "slice has no quantization table set indexes");
    }
    const auto expected_index_count = stream_.version >= 3
        ? syntax::quant_table_set_index_count(stream_)
        : std::size_t{1};
    if (slice.quant_table_set_indexes.size() != expected_index_count) {
        return make_error(ErrorCode::SyntaxError,
                          "slice quantization table set index count does not match the stream");
    }
    for (const auto quant_table_set_index : slice.quant_table_set_indexes) {
        if (quant_table_set_index >= stream_.quant_table_sets.size()) {
            return make_error(ErrorCode::SyntaxError, "slice quantization table set index is out of range");
        }
        if (stream_.entropy_mode == EntropyMode::Range && !stream_.initial_states.empty()) {
            if (stream_.initial_states.size() != stream_.quant_table_sets.size()) {
                return make_error(ErrorCode::InvalidState,
                                  "range coder initial state set count does not match quantization table set count");
            }
            const auto& states = stream_.initial_states[quant_table_set_index].contexts;
            if (!states.empty()
                && states.size() != stream_.quant_table_sets[quant_table_set_index].context_count) {
                return make_error(ErrorCode::InvalidState,
                                  "range coder initial state count does not match quantization context count");
            }
        }
    }
    return ok_status();
}

Status SliceDecoder::decode(const syntax::SliceDescriptor& slice,
                            SliceOutputWindow& output,
                            SliceState& state) const
{
    Status status = validate(slice, output);
    if (!status.ok()) {
        return status;
    }
    if (output.plane_count() != state.plane_count()) {
        return make_error(ErrorCode::InvalidArgument, "slice output and state plane counts differ");
    }

    ByteSpan content_payload;
    status = resolve_content_payload(slice, content_payload);
    if (!status.ok()) {
        return status;
    }
    std::vector<syntax::ContextModel> context_models;
    std::vector<std::size_t> context_counts;
    std::vector<std::span<const entropy::RangeCoder::ScalarContextStates>> initial_state_banks;
    context_models.reserve(output.plane_count());
    context_counts.reserve(output.plane_count());
    initial_state_banks.reserve(output.plane_count());
    for (std::size_t plane_index = 0; plane_index < output.plane_count(); ++plane_index) {
        const auto index_slot = stream_.version >= 3
            ? syntax::plane_quant_table_set_index_slot(stream_, plane_index)
            : std::size_t{0};
        const auto quant_table_set_index = slice.quant_table_set_indexes[index_slot];
        context_models.emplace_back(stream_.quant_table_sets[quant_table_set_index]);
        context_counts.push_back(context_models.back().context_count());
        if (stream_.initial_states.empty()) {
            initial_state_banks.emplace_back();
        } else {
            initial_state_banks.emplace_back(stream_.initial_states[quant_table_set_index].contexts);
        }
    }

    if (stream_.entropy_mode == EntropyMode::Range && state.has_range_contexts()) {
        const auto& saved_contexts = state.range_contexts();
        if (saved_contexts.size() != context_counts.size()) {
            return make_error(ErrorCode::InvalidState,
                              "saved range context bank count does not match slice plane count");
        }
        initial_state_banks.clear();
        for (std::size_t plane_index = 0; plane_index < saved_contexts.size(); ++plane_index) {
            if (saved_contexts[plane_index].size() != context_counts[plane_index]) {
                return make_error(ErrorCode::InvalidState,
                                  "saved range context count does not match quantization contexts");
            }
            initial_state_banks.emplace_back(saved_contexts[plane_index]);
        }
    }

    if (stream_.entropy_mode == EntropyMode::GolombRice) {
        return decode_golomb_rice_slice(stream_,
                                        kernels_,
                                        content_payload,
                                        slice.content_byte_offset,
                                        slice.content_bit_offset,
                                        context_models,
                                        output,
                                        state);
    }

    entropy::RangeCoder reader;
    std::uint64_t reader_base_offset = slice.content_byte_offset;
    const bool continue_from_slice_header = stream_.version >= 3 && slice.slice_size != 0;
    const bool continue_from_legacy_frame_header = slice.continues_frame_range_state;
    if (continue_from_slice_header) {
        const auto local_footer_offset = slice.footer_byte_offset - slice.payload_byte_offset;
        const auto entropy_payload = slice.payload.first(static_cast<std::size_t>(local_footer_offset));
        status = reader.reset(entropy_payload, stream_.state_transition);
        if (!status.ok()) {
            add_byte_offset(status, slice.payload_byte_offset);
            return status;
        }

        if (slice.index == 0) {
            bool keyframe = false;
            status = reader.read_bool(keyframe);
            if (!status.ok()) {
                add_byte_offset(status, slice.payload_byte_offset);
                return status;
            }
            if (!keyframe && stream_.intra_only) {
                return make_byte_error(ErrorCode::SyntaxError,
                                       "non-keyframe is invalid for an intra-only stream",
                                       slice.payload_byte_offset);
            }
        }

        syntax::SliceDescriptor encoded_slice;
        const SliceHeaderParser header_parser;
        status = header_parser.read_descriptor(reader, stream_, encoded_slice);
        if (!status.ok()) {
            add_byte_offset(status, slice.payload_byte_offset);
            return status;
        }
        const auto local_content_offset = slice.content_byte_offset - slice.payload_byte_offset;
        if (encoded_slice.raster_x != slice.raster_x
            || encoded_slice.raster_y != slice.raster_y
            || encoded_slice.raster_width != slice.raster_width
            || encoded_slice.raster_height != slice.raster_height
            || encoded_slice.quant_table_set_indexes != slice.quant_table_set_indexes
            || encoded_slice.picture_structure != slice.picture_structure
            || encoded_slice.sar_num != slice.sar_num
            || encoded_slice.sar_den != slice.sar_den
            || encoded_slice.content_byte_offset != local_content_offset) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice descriptor does not match its encoded header",
                                   slice.header_byte_offset);
        }

        status = reader.reconfigure_contexts(context_counts, initial_state_banks);
        reader_base_offset = slice.payload_byte_offset;
    } else if (continue_from_legacy_frame_header) {
        status = reader.reset(slice.payload, stream_.state_transition);
        if (!status.ok()) {
            add_byte_offset(status, slice.payload_byte_offset);
            return status;
        }
        bool keyframe = false;
        status = reader.read_bool(keyframe);
        if (!status.ok()) {
            add_byte_offset(status, slice.payload_byte_offset);
            return status;
        }
        if (!keyframe && stream_.intra_only) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "non-keyframe is invalid for an intra-only stream",
                                   slice.payload_byte_offset);
        }
        const auto local_content_offset = slice.content_byte_offset - slice.payload_byte_offset;
        if (reader.byte_position() != local_content_offset) {
            return make_byte_error(ErrorCode::SyntaxError,
                                   "slice descriptor does not match the legacy frame header",
                                   slice.header_byte_offset);
        }
        status = reader.reconfigure_contexts(context_counts, initial_state_banks);
        reader_base_offset = slice.payload_byte_offset;
    } else {
        status = reader.reset(content_payload,
                              context_counts,
                              initial_state_banks,
                              stream_.state_transition);
    }
    if (!status.ok()) {
        add_byte_offset(status, reader_base_offset);
        return status;
    }

    if (stream_.colorspace_type == 1) {
        const auto coded_bits = static_cast<std::uint8_t>(stream_.bits_per_raw_sample + 1);
        const auto width = output.plane_width(0);
        const auto height = output.plane_height(0);
        std::array<std::vector<std::uint16_t>, 3> rgb_scratch;
        for (auto& plane : rgb_scratch) {
            plane.resize(width);
        }
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::size_t plane_index = 0; plane_index < output.plane_count(); ++plane_index) {
                auto& line = state.line_state(plane_index);
                const auto reconstruction_bits = plane_index < 3
                    ? coded_bits
                    : stream_.bits_per_raw_sample;
                status = decode_range_line(reader,
                                           reader_base_offset,
                                           context_models[plane_index],
                                           plane_index,
                                           width,
                                           reconstruction_bits,
                                           false,
                                           line);
                if (!status.ok()) {
                    return status;
                }
            }
            status = store_rgb_line(
                stream_, kernels_, output, y, state, rgb_scratch);
            if (!status.ok()) {
                return status;
            }
        }
        return state.capture_range_contexts(reader);
    }

    const bool use_signed_16bit_prediction = syntax::uses_signed_16bit_predictor(stream_);

    for (std::size_t plane_index = 0; plane_index < output.plane_count(); ++plane_index) {
        const auto& context_model = context_models[plane_index];
        auto& line = state.line_state(plane_index);
        for (std::uint32_t y = 0; y < output.plane_height(plane_index); ++y) {
            const auto width = output.plane_width(plane_index);
            status = decode_range_line(reader,
                                       reader_base_offset,
                                       context_model,
                                       plane_index,
                                       width,
                                       stream_.bits_per_raw_sample,
                                       use_signed_16bit_prediction,
                                       line);
            if (!status.ok()) {
                return status;
            }
            status = store_planar_line(stream_, output, plane_index, y, line);
            if (!status.ok()) {
                return status;
            }
        }
    }

    return state.capture_range_contexts(reader);
}

} // namespace mffv1::codec
