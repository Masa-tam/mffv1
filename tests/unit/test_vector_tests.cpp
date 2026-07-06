#include "test_vector_data.hpp"

#include "codec/configuration_record_parser.hpp"
#include "codec/frame_parser.hpp"
#include "codec/slice_decoder.hpp"
#include "codec/slice_output_window.hpp"
#include "mffv1/color_transform.hpp"
#include "mffv1/codec.hpp"
#include "mffv1/predictor.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#if !defined(NO_DEFINE_TEST_VECTOR_DATA)
namespace {

bool compute_plane_size(const mffv1_testvectors::PlaneVector& plane,
                        std::size_t& out_size);

std::uint32_t read_sample(std::span<const std::byte> bytes,
                          std::size_t sample_offset,
                          mffv1::SampleFormat format);

std::string describe_status(const mffv1::Status& status)
{
    std::ostringstream out;
    out << status.message;
    if (status.location.has_byte_offset) {
        out << " byte=" << status.location.byte_offset;
    }
    if (status.location.has_slice_index) {
        out << " slice=" << status.location.slice_index;
    }
    return out.str();
}

std::string describe_frame_parse(
    const mffv1_testvectors::DecodeVector& vector,
    std::span<const std::byte> frame_payload)
{
    mffv1::syntax::StreamParameters stream;
    mffv1::codec::ConfigurationRecordParser config_parser;
    auto status = config_parser.parse(vector.configuration_record, stream);
    if (!status.ok()) {
        return std::string{"config parse: "} + describe_status(status);
    }
    stream.width = vector.frame_width;
    stream.height = vector.frame_height;

    mffv1::codec::FrameParser frame_parser(stream, true);
    mffv1::codec::FrameDecodeContext frame;
    status = frame_parser.parse(frame_payload, frame);
    if (!status.ok()) {
        return std::string{"frame parse: "} + describe_status(status);
    }

    std::ostringstream out;
    out << "stream entropy="
        << (stream.entropy_mode == mffv1::EntropyMode::GolombRice ? "gr" : "range")
        << " version=" << stream.version
        << "." << stream.micro_version
        << " colorspace=" << stream.colorspace_type
        << " bits=" << static_cast<int>(stream.bits_per_raw_sample)
        << " chroma=" << stream.chroma_planes
        << " subsample=" << static_cast<int>(stream.log2_h_chroma_subsample)
        << "," << static_cast<int>(stream.log2_v_chroma_subsample)
        << " extra=" << stream.extra_plane
        << " grid=" << stream.num_h_slices << "x" << stream.num_v_slices
        << " qsets=" << stream.quant_table_sets.size()
        << " state8=" << static_cast<int>(stream.state_transition[8])
        << " state128=" << static_cast<int>(stream.state_transition[128])
        << " statesets=" << stream.initial_states.size();
    for (std::size_t i = 0; i < stream.quant_table_sets.size(); ++i) {
        out << " q" << i << "=" << stream.quant_table_sets[i].context_count;
        if (i < stream.initial_states.size()) {
            out << "/states" << stream.initial_states[i].contexts.size();
        }
    }
    out
        << " slices=" << frame.slices.size();
    for (const auto& slice : frame.slices) {
        out << " [#" << slice.index
            << " x=" << slice.x
            << " y=" << slice.y
            << " w=" << slice.width
            << " h=" << slice.height
            << " raster=" << slice.raster_x << "," << slice.raster_y
            << "+" << slice.raster_width << "x" << slice.raster_height
            << " payload=" << slice.payload_byte_offset
            << " content=" << slice.content_byte_offset
            << " footer=" << slice.footer_byte_offset
            << " size=" << slice.slice_size
            << " qidx=";
        for (const auto index : slice.quant_table_set_indexes) {
            out << index << ",";
        }
        out << "]";
    }
    return out.str();
}

std::uint32_t sample_at(std::span<const std::byte> bytes,
                        const mffv1::PlaneInfo& plane,
                        std::uint32_t x,
                        std::uint32_t y)
{
    const auto bytes_per_sample = plane.sample_format == mffv1::SampleFormat::UInt16
        ? std::size_t{2}
        : std::size_t{1};
    const auto offset = static_cast<std::size_t>(y)
            * static_cast<std::size_t>(plane.stride_bytes)
        + static_cast<std::size_t>(x) * bytes_per_sample;
    return read_sample(bytes, offset, plane.sample_format);
}

std::uint32_t previous_sample_or_zero(std::span<const std::byte> bytes,
                                      const mffv1::PlaneInfo& plane,
                                      std::uint32_t x,
                                      std::uint32_t y)
{
    return y == 0 ? 0 : sample_at(bytes, plane, x, y - 1);
}

std::uint32_t second_previous_sample_or_zero(std::span<const std::byte> bytes,
                                             const mffv1::PlaneInfo& plane,
                                             std::uint32_t x,
                                             std::uint32_t y)
{
    return y < 2 ? 0 : sample_at(bytes, plane, x, y - 2);
}

std::string describe_mismatch_neighbors(std::span<const std::byte> bytes,
                                        const mffv1::PlaneInfo& plane,
                                        std::uint32_t x,
                                        std::uint32_t y)
{
    const auto far_left = x > 1
        ? sample_at(bytes, plane, x - 2, y)
        : (x == 1 ? previous_sample_or_zero(bytes, plane, 0, y) : 0);
    const auto left = x > 0
        ? sample_at(bytes, plane, x - 1, y)
        : previous_sample_or_zero(bytes, plane, 0, y);
    const auto top = previous_sample_or_zero(bytes, plane, x, y);
    const auto top_left = x > 0
        ? previous_sample_or_zero(bytes, plane, x - 1, y)
        : second_previous_sample_or_zero(bytes, plane, 0, y);
    const auto top_right = (x + 1) < plane.width
        ? previous_sample_or_zero(bytes, plane, x + 1, y)
        : previous_sample_or_zero(bytes, plane, x, y);
    const auto top_top = second_previous_sample_or_zero(bytes, plane, x, y);
    std::ostringstream out;
    out << "L/l/t/tl/tr/T="
        << far_left << "/"
        << left << "/"
        << top << "/"
        << top_left << "/"
        << top_right << "/"
        << top_top;
    return out.str();
}

std::string describe_mismatch_prediction(std::span<const std::byte> bytes,
                                         const mffv1::PlaneInfo& plane,
                                         std::uint32_t x,
                                         std::uint32_t y,
                                         std::uint8_t bits_per_raw_sample)
{
    const auto left = x > 0
        ? sample_at(bytes, plane, x - 1, y)
        : previous_sample_or_zero(bytes, plane, 0, y);
    const auto top = previous_sample_or_zero(bytes, plane, x, y);
    const auto top_left = x > 0
        ? previous_sample_or_zero(bytes, plane, x - 1, y)
        : second_previous_sample_or_zero(bytes, plane, 0, y);
    const auto prediction = mffv1::syntax::Predictor::median_predict(
        static_cast<std::int32_t>(left),
        static_cast<std::int32_t>(top),
        static_cast<std::int32_t>(top_left));
    const auto sample = sample_at(bytes, plane, x, y);
    const auto difference = mffv1::syntax::Predictor::difference(
        static_cast<std::int32_t>(sample),
        prediction,
        bits_per_raw_sample);

    std::ostringstream out;
    out << "pred=" << prediction
        << " diff=" << difference;
    return out.str();
}

std::string describe_first_partial_mismatch(
    std::span<const std::byte> actual,
    std::span<const std::byte> expected,
    std::size_t plane_index,
    const mffv1::PlaneInfo& plane,
    std::uint8_t bits_per_raw_sample)
{
    const auto bytes_per_sample = plane.sample_format == mffv1::SampleFormat::UInt16
        ? std::size_t{2}
        : std::size_t{1};
    const auto stride = static_cast<std::size_t>(plane.stride_bytes);
    const auto active_row_bytes = static_cast<std::size_t>(plane.width) * bytes_per_sample;
    for (std::uint32_t y = 0; y < plane.height; ++y) {
        const auto row_offset = static_cast<std::size_t>(y) * stride;
        const auto actual_row = actual.subspan(row_offset, active_row_bytes);
        const auto expected_row = expected.subspan(row_offset, active_row_bytes);
        const auto mismatch = std::mismatch(
            actual_row.begin(), actual_row.end(), expected_row.begin());
        auto actual_it = mismatch.first;
        while (actual_it != actual_row.end() && *actual_it == std::byte{0xa5}) {
            ++actual_it;
        }
        if (actual_it == actual_row.end()) {
            continue;
        }

        const auto byte_offset = row_offset
            + static_cast<std::size_t>(actual_it - actual_row.begin());
        const auto row_byte = byte_offset % stride;
        const auto x = row_byte / bytes_per_sample;
        const auto sample_offset = byte_offset - (row_byte % bytes_per_sample);
        std::ostringstream out;
        out << "partial plane " << plane_index
            << " first mismatch at x=" << x
            << " y=" << y
            << " byte=" << byte_offset
            << " actual_sample=" << read_sample(actual, sample_offset, plane.sample_format)
            << " expected_sample=" << read_sample(expected, sample_offset, plane.sample_format)
            << " actual_neighbors="
            << describe_mismatch_neighbors(
                   actual, plane, static_cast<std::uint32_t>(x), y)
            << " expected_neighbors="
            << describe_mismatch_neighbors(
                   expected, plane, static_cast<std::uint32_t>(x), y)
            << " actual_prediction="
            << describe_mismatch_prediction(
                   actual, plane, static_cast<std::uint32_t>(x), y, bits_per_raw_sample)
            << " expected_prediction="
            << describe_mismatch_prediction(
                   expected, plane, static_cast<std::uint32_t>(x), y, bits_per_raw_sample);
        return out.str();
    }
    return {};
}

std::string describe_adaptive_state(const mffv1::entropy::GolombRiceContextState& state)
{
    std::ostringstream out;
    out << state.drift << "/"
        << state.error_sum << "/"
        << state.bias << "/"
        << state.count;
    return out.str();
}

std::uint8_t derive_golomb_rice_k_for_diagnostic(
    const mffv1::entropy::GolombRiceContextState& state)
{
    auto threshold = state.count;
    std::uint8_t k = 0;
    while (threshold < state.error_sum && k < 31) {
        ++k;
        threshold *= 2;
    }
    return k;
}

std::string describe_bit_range(std::span<const std::byte> bytes,
                               std::uint64_t begin_bit,
                               std::uint64_t end_bit)
{
    std::ostringstream out;
    for (auto bit = begin_bit; bit < end_bit; ++bit) {
        const auto byte_index = static_cast<std::size_t>(bit / 8);
        if (byte_index >= bytes.size()) {
            out << "?";
            continue;
        }
        const auto bit_index = 7u - static_cast<unsigned>(bit % 8);
        const auto value =
            (std::to_integer<std::uint8_t>(bytes[byte_index]) >> bit_index) & 1u;
        out << value;
    }
    return out.str();
}

std::string describe_bit_window(std::span<const std::byte> bytes,
                                std::uint64_t center_bit,
                                std::uint64_t radius)
{
    const auto begin_bit = center_bit > radius ? center_bit - radius : 0;
    const auto end_bit = center_bit > std::numeric_limits<std::uint64_t>::max() - radius
        ? std::numeric_limits<std::uint64_t>::max()
        : center_bit + radius;

    std::ostringstream out;
    out << begin_bit << "-" << end_bit
        << "(" << describe_bit_range(bytes, begin_bit, end_bit) << ")";
    return out.str();
}

std::uint8_t diagnostic_context_input_index(std::int64_t value) noexcept
{
    return static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) & 0xffu);
}

std::string describe_context_terms(
    const mffv1::syntax::NeighborSamples& samples,
    const mffv1::syntax::QuantTableSet& table_set)
{
    const std::array<std::int64_t, mffv1::syntax::QuantTableSet::kContextInputs> gradients{
        static_cast<std::int64_t>(samples.left) - samples.top_left,
        static_cast<std::int64_t>(samples.top_left) - samples.top,
        static_cast<std::int64_t>(samples.top) - samples.top_right,
        static_cast<std::int64_t>(samples.far_left) - samples.left,
        static_cast<std::int64_t>(samples.top_top) - samples.top,
    };
    std::ostringstream out;
    out << " grads=";
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        if (i != 0) {
            out << "/";
        }
        out << gradients[i];
    }
    out << " terms=";
    for (std::size_t i = 0; i < gradients.size(); ++i) {
        if (i != 0) {
            out << "/";
        }
        out << table_set.tables[i][diagnostic_context_input_index(gradients[i])];
    }
    return out.str();
}

std::string describe_compact_trace(
    const mffv1::codec::GolombRiceSampleTrace& trace,
    std::span<const std::byte> content_payload)
{
    const auto rice_k =
        derive_golomb_rice_k_for_diagnostic(trace.adaptive_state_before);
    std::ostringstream out;
    out << "p" << trace.plane
        << ":" << trace.y << "," << trace.x
        << " c" << trace.context.context
        << (trace.context.invert_difference ? "i" : "")
        << (trace.run_interruption ? " ri" : "")
        << " b" << trace.bit_position_before
        << "-" << trace.bit_position_after
        << "("
        << describe_bit_range(
               content_payload, trace.bit_position_before, trace.bit_position_after)
        << ")"
        << " k" << static_cast<int>(rice_k)
        << " d" << trace.difference
        << " s" << trace.reconstructed_sample
        << " r"
        << static_cast<int>(trace.run_state_before.run_index)
        << "/" << trace.run_state_before.pending_count
        << ">"
        << static_cast<int>(trace.run_state_after.run_index)
        << "/" << trace.run_state_after.pending_count
        << " a"
        << trace.adaptive_state_before.drift << "/"
        << trace.adaptive_state_before.error_sum << "/"
        << trace.adaptive_state_before.bias << "/"
        << trace.adaptive_state_before.count
        << ">"
        << trace.adaptive_state_after.drift << "/"
        << trace.adaptive_state_after.error_sum << "/"
        << trace.adaptive_state_after.bias << "/"
        << trace.adaptive_state_after.count;
    return out.str();
}

std::string describe_rgb_internal_candidates(
    std::span<const mffv1_testvectors::PlaneVector> expected_planes,
    const mffv1::syntax::StreamParameters& stream,
    const mffv1::codec::GolombRiceSampleTrace& trace)
{
    if (stream.colorspace_type != 1 || trace.plane > 2 || expected_planes.size() < 3) {
        return {};
    }

    const auto p0 = sample_at(
        expected_planes[0].samples, expected_planes[0].info, trace.x, trace.y);
    const auto p1 = sample_at(
        expected_planes[1].samples, expected_planes[1].info, trace.x, trace.y);
    const auto p2 = sample_at(
        expected_planes[2].samples, expected_planes[2].info, trace.x, trace.y);
    const auto as_rgb = mffv1::syntax::forward_jpeg2000_rct(
        static_cast<std::uint16_t>(p0),
        static_cast<std::uint16_t>(p1),
        static_cast<std::uint16_t>(p2),
        stream.bits_per_raw_sample,
        stream.extra_plane);
    const auto as_gbr = mffv1::syntax::forward_jpeg2000_rct(
        static_cast<std::uint16_t>(p2),
        static_cast<std::uint16_t>(p0),
        static_cast<std::uint16_t>(p1),
        stream.bits_per_raw_sample,
        stream.extra_plane);

    const std::array<std::int32_t, 3> rgb_values{as_rgb.y, as_rgb.cb, as_rgb.cr};
    const std::array<std::int32_t, 3> gbr_values{as_gbr.y, as_gbr.cb, as_gbr.cr};
    std::ostringstream out;
    out << " rgb_candidates p0/p1/p2="
        << p0 << "/" << p1 << "/" << p2
        << " as_rgb="
        << rgb_values[0] << "/" << rgb_values[1] << "/" << rgb_values[2]
        << " as_gbr="
        << gbr_values[0] << "/" << gbr_values[1] << "/" << gbr_values[2]
        << " active="
        << rgb_values[trace.plane] << "/" << gbr_values[trace.plane];
    return out.str();
}

class FirstGolombRiceMismatchObserver final : public mffv1::codec::SliceDecodeObserver {
public:
    explicit FirstGolombRiceMismatchObserver(
        std::span<const mffv1_testvectors::PlaneVector> expected_planes,
        std::span<const std::byte> content_payload,
        std::span<const mffv1::syntax::QuantTableSet> quant_table_sets,
        std::span<const std::uint32_t> plane_quant_table_set_indexes,
        const mffv1::syntax::StreamParameters& stream)
        : expected_planes_(expected_planes),
          content_payload_(content_payload),
          quant_table_sets_(quant_table_sets),
          plane_quant_table_set_indexes_(plane_quant_table_set_indexes),
          stream_(stream)
    {
    }

    void on_golomb_rice_sample(
        const mffv1::codec::GolombRiceSampleTrace& trace) override
    {
        if (!description_.empty() || trace.plane >= expected_planes_.size()) {
            return;
        }
        const auto& expected = expected_planes_[trace.plane];
        if (trace.x >= expected.info.width || trace.y >= expected.info.height) {
            return;
        }
        const auto expected_sample = expected_internal_sample(trace);
        if (static_cast<std::uint32_t>(trace.reconstructed_sample) == expected_sample) {
            ++matched_sample_count_;
            remember_trace(trace);
            return;
        }
        const auto expected_difference = mffv1::syntax::Predictor::difference(
            static_cast<std::int32_t>(expected_sample),
            trace.prediction,
            stream_.bits_per_raw_sample);
        const auto rice_k =
            derive_golomb_rice_k_for_diagnostic(trace.adaptive_state_before);
        const auto has_context_terms =
            trace.plane < plane_quant_table_set_indexes_.size()
            && plane_quant_table_set_indexes_[trace.plane] < quant_table_sets_.size();

        std::ostringstream out;
        out << "first traced GR mismatch plane=" << trace.plane
            << " x=" << trace.x
            << " y=" << trace.y
            << " context=" << trace.context.context
            << (trace.context.invert_difference ? " invert=1" : " invert=0")
            << (trace.run_interruption ? " run_interruption=1" : " run_interruption=0")
            << " bits=" << trace.bit_position_before
            << "-" << trace.bit_position_after
            << "("
            << describe_bit_range(
                   content_payload_, trace.bit_position_before, trace.bit_position_after)
            << ")"
            << " bit_window="
            << describe_bit_window(content_payload_, trace.bit_position_before, 16)
            << " k=" << static_cast<int>(rice_k)
            << " pred=" << trace.prediction
            << " actual_sample=" << trace.reconstructed_sample
            << " expected_sample=" << expected_sample
            << " actual_diff=" << trace.difference
            << " expected_diff=" << expected_difference
            << describe_rgb_internal_candidates(expected_planes_, stream_, trace)
            << " state_before="
            << describe_adaptive_state(trace.adaptive_state_before)
            << " state_after="
            << describe_adaptive_state(trace.adaptive_state_after)
            << " run_before="
            << static_cast<int>(trace.run_state_before.run_index)
            << "/" << trace.run_state_before.pending_count
            << " run_after="
            << static_cast<int>(trace.run_state_after.run_index)
            << "/" << trace.run_state_after.pending_count;
        if (has_context_terms) {
            out << describe_context_terms(
                trace.neighbors,
                quant_table_sets_[plane_quant_table_set_indexes_[trace.plane]]);
        }
        if (!recent_traces_.empty()) {
            out << " previous=[";
            for (std::size_t i = 0; i < recent_traces_.size(); ++i) {
                if (i != 0) {
                    out << "; ";
                }
                out << recent_traces_[i];
            }
            out << "]";
        }
        description_ = out.str();
    }

    const std::string& description() const noexcept
    {
        return description_;
    }

    std::size_t matched_sample_count() const noexcept
    {
        return matched_sample_count_;
    }

private:
    std::uint32_t expected_internal_sample(
        const mffv1::codec::GolombRiceSampleTrace& trace) const
    {
        if (stream_.colorspace_type != 1 || trace.plane > 2
            || expected_planes_.size() < 3) {
            const auto& expected = expected_planes_[trace.plane];
            return sample_at(expected.samples, expected.info, trace.x, trace.y);
        }

        const auto r = sample_at(
            expected_planes_[0].samples, expected_planes_[0].info, trace.x, trace.y);
        const auto g = sample_at(
            expected_planes_[1].samples, expected_planes_[1].info, trace.x, trace.y);
        const auto b = sample_at(
            expected_planes_[2].samples, expected_planes_[2].info, trace.x, trace.y);
        const auto code = mffv1::syntax::forward_jpeg2000_rct(
            static_cast<std::uint16_t>(r),
            static_cast<std::uint16_t>(g),
            static_cast<std::uint16_t>(b),
            stream_.bits_per_raw_sample,
            stream_.extra_plane);
        if (trace.plane == 0) {
            return static_cast<std::uint32_t>(code.y);
        }
        if (trace.plane == 1) {
            return static_cast<std::uint32_t>(code.cb);
        }
        return static_cast<std::uint32_t>(code.cr);
    }

    void remember_trace(const mffv1::codec::GolombRiceSampleTrace& trace)
    {
        if (recent_traces_.size() == kRecentTraceLimit) {
            recent_traces_.erase(recent_traces_.begin());
        }
        recent_traces_.push_back(describe_compact_trace(trace, content_payload_));
    }

    static constexpr std::size_t kRecentTraceLimit = 8;
    std::span<const mffv1_testvectors::PlaneVector> expected_planes_;
    std::span<const std::byte> content_payload_;
    std::span<const mffv1::syntax::QuantTableSet> quant_table_sets_;
    std::span<const std::uint32_t> plane_quant_table_set_indexes_;
    const mffv1::syntax::StreamParameters& stream_;
    std::vector<std::string> recent_traces_;
    std::string description_;
    std::size_t matched_sample_count_ = 0;
};

std::string describe_golomb_rice_candidate_decode(
    const mffv1::syntax::StreamParameters& stream,
    const mffv1::syntax::SliceDescriptor& candidate,
    std::span<const mffv1_testvectors::PlaneVector> expected_planes,
    std::string_view label)
{
    std::vector<std::vector<std::byte>> plane_storage;
    std::vector<mffv1::MutablePlaneView> output_planes;
    plane_storage.reserve(expected_planes.size());
    output_planes.reserve(expected_planes.size());
    for (const auto& expected : expected_planes) {
        std::size_t expected_size = 0;
        if (!compute_plane_size(expected, expected_size)) {
            return {};
        }
        plane_storage.emplace_back(expected_size, std::byte{0xa5});
        output_planes.push_back(
            mffv1::MutablePlaneView{plane_storage.back().data(), expected.info});
    }

    mffv1::MutableFrameView output{output_planes.data(), output_planes.size()};
    mffv1::codec::SliceOutputWindow window;
    auto status = window.validate(stream, output, candidate);
    if (!status.ok()) {
        std::ostringstream out;
        out << label << " candidate status: " << describe_status(status);
        return out.str();
    }
    mffv1::codec::SliceState state;
    status = state.reset(stream, window);
    if (!status.ok()) {
        std::ostringstream out;
        out << label << " candidate status: " << describe_status(status);
        return out.str();
    }
    const auto content_offset = candidate.content_byte_offset - candidate.payload_byte_offset;
    const auto content_payload = candidate.payload.subspan(content_offset);
    std::vector<std::uint32_t> plane_quant_table_set_indexes;
    plane_quant_table_set_indexes.reserve(output_planes.size());
    for (std::size_t plane = 0; plane < output_planes.size(); ++plane) {
        const auto index_slot = stream.version >= 3
            ? mffv1::syntax::plane_quant_table_set_index_slot(stream, plane)
            : plane;
        if (index_slot >= candidate.quant_table_set_indexes.size()) {
            return {};
        }
        plane_quant_table_set_indexes.push_back(candidate.quant_table_set_indexes[index_slot]);
    }
    FirstGolombRiceMismatchObserver observer(
        expected_planes,
        content_payload,
        stream.quant_table_sets,
        plane_quant_table_set_indexes,
        stream);
    const mffv1::codec::SliceDecoder slice_decoder(stream);
    status = slice_decoder.decode(candidate, window, state, &observer);

    std::ostringstream out;
    out << label << " candidate byte=" << candidate.content_byte_offset
        << " bit=" << static_cast<int>(candidate.content_bit_offset)
        << " matched_samples=" << observer.matched_sample_count()
        << " status: " << describe_status(status);
    if (!observer.description().empty()) {
        out << "\n" << observer.description();
    }
    for (std::size_t index = 0; index < expected_planes.size(); ++index) {
        const bool plane_was_touched = std::any_of(
            plane_storage[index].begin(),
            plane_storage[index].end(),
            [](std::byte value) { return value != std::byte{0xa5}; });
        if (!plane_was_touched) {
            continue;
        }
        const auto& expected = expected_planes[index];
        std::size_t expected_size = 0;
        if (!compute_plane_size(expected, expected_size)) {
            continue;
        }
        const std::vector<std::byte> expected_bytes{
            expected.samples.begin(), expected.samples.begin() + expected_size};
        const auto mismatch = describe_first_partial_mismatch(
            plane_storage[index],
            expected_bytes,
            index,
            expected.info,
            stream.bits_per_raw_sample);
        if (!mismatch.empty()) {
            out << "\n" << mismatch;
            break;
        }
    }
    return out.str();
}

std::string describe_golomb_rice_partial_decode(
    const mffv1_testvectors::DecodeVector& vector,
    std::span<const std::byte> frame_payload,
    std::span<const mffv1_testvectors::PlaneVector> expected_planes)
{
    mffv1::syntax::StreamParameters stream;
    mffv1::codec::ConfigurationRecordParser config_parser;
    auto status = config_parser.parse(vector.configuration_record, stream);
    if (!status.ok()) {
        return {};
    }
    if (stream.entropy_mode != mffv1::EntropyMode::GolombRice
        || stream.version < 3) {
        return {};
    }
    stream.width = vector.frame_width;
    stream.height = vector.frame_height;

    mffv1::codec::FrameParser frame_parser(stream, true);
    mffv1::codec::FrameDecodeContext frame;
    status = frame_parser.parse(frame_payload, frame);
    if (!status.ok() || frame.slices.empty()) {
        return {};
    }

    std::ostringstream out;
    const auto& primary = frame.slices.front();
    out << describe_golomb_rice_candidate_decode(
        stream, primary, expected_planes, "partial primary");
    for (std::uint8_t bit_offset = 1; bit_offset < 8; ++bit_offset) {
        auto shifted = primary;
        shifted.content_bit_offset = bit_offset;
        out << "\n" << describe_golomb_rice_candidate_decode(
            stream, shifted, expected_planes, "partial primary-bit");
    }
    if (primary.content_byte_offset > primary.payload_byte_offset) {
        auto read_ahead = primary;
        --read_ahead.content_byte_offset;
        out << "\n" << describe_golomb_rice_candidate_decode(
            stream, read_ahead, expected_planes, "partial read-ahead");
    }
    return out.str();
}

bool compute_plane_size(const mffv1_testvectors::PlaneVector& plane,
                        std::size_t& out_size)
{
    EXPECT_GT(plane.info.width, 0u);
    EXPECT_GT(plane.info.height, 0u);
    EXPECT_GT(plane.info.stride_bytes, 0);
    if (plane.info.width == 0 || plane.info.height == 0 ||
        plane.info.stride_bytes <= 0) {
        return false;
    }

    const auto stride = static_cast<std::size_t>(plane.info.stride_bytes);
    const auto height = static_cast<std::size_t>(plane.info.height);
    if (stride > std::numeric_limits<std::size_t>::max() / height) {
        ADD_FAILURE() << "plane byte size overflows size_t";
        return false;
    }

    out_size = stride * height;
    if (plane.samples.size() < out_size) {
        ADD_FAILURE() << "plane samples are shorter than stride * height";
        return false;
    }
    return true;
}

std::uint8_t byte_value(std::byte value) noexcept
{
    return std::to_integer<std::uint8_t>(value);
}

std::uint32_t read_sample(std::span<const std::byte> bytes,
                          std::size_t sample_offset,
                          mffv1::SampleFormat format)
{
    if (format == mffv1::SampleFormat::UInt8) {
        return byte_value(bytes[sample_offset]);
    }
    return static_cast<std::uint32_t>(byte_value(bytes[sample_offset]))
        | (static_cast<std::uint32_t>(byte_value(bytes[sample_offset + 1])) << 8);
}

void report_plane_mismatch(std::span<const std::byte> actual,
                           std::span<const std::byte> expected,
                           std::size_t byte_offset,
                           std::size_t plane_index,
                           const mffv1::PlaneInfo& plane,
                           const std::string& frame_description)
{
    const auto bytes_per_sample = plane.sample_format == mffv1::SampleFormat::UInt16
        ? std::size_t{2}
        : std::size_t{1};
    const auto stride = static_cast<std::size_t>(plane.stride_bytes);
    const auto y = byte_offset / stride;
    const auto row_byte = byte_offset % stride;
    const auto x = row_byte / bytes_per_sample;
    const auto sample_offset = byte_offset - (row_byte % bytes_per_sample);
    const auto actual_sample = read_sample(actual, sample_offset, plane.sample_format);
    const auto expected_sample = read_sample(expected, sample_offset, plane.sample_format);

    ADD_FAILURE()
        << "plane " << plane_index
        << " mismatch at byte " << byte_offset
        << " (x=" << x << " y=" << y << " row_byte=" << row_byte << ")"
        << " actual_byte=0x" << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<int>(byte_value(actual[byte_offset]))
        << " expected_byte=0x" << std::setw(2)
        << static_cast<int>(byte_value(expected[byte_offset]))
        << std::dec
        << " actual_sample=" << actual_sample
        << " expected_sample=" << expected_sample
        << " width=" << plane.width
        << " height=" << plane.height
        << " stride=" << plane.stride_bytes
        << "\n" << frame_description;
}

void expect_plane_matches(std::span<const std::byte> actual,
                          std::span<const std::byte> expected,
                          std::size_t plane_index,
                          const mffv1::PlaneInfo& plane,
                          const std::string& frame_description)
{
    ASSERT_EQ(actual.size(), expected.size());
    const auto bytes_per_sample = plane.sample_format == mffv1::SampleFormat::UInt16
        ? std::size_t{2}
        : std::size_t{1};
    const auto stride = static_cast<std::size_t>(plane.stride_bytes);
    const auto active_row_bytes = static_cast<std::size_t>(plane.width) * bytes_per_sample;
    ASSERT_LE(active_row_bytes, stride);
    for (std::uint32_t y = 0; y < plane.height; ++y) {
        const auto row_offset = static_cast<std::size_t>(y) * stride;
        const auto actual_row = actual.subspan(row_offset, active_row_bytes);
        const auto expected_row = expected.subspan(row_offset, active_row_bytes);
        const auto mismatch = std::mismatch(
            actual_row.begin(), actual_row.end(), expected_row.begin());
        if (mismatch.first != actual_row.end()) {
            const auto byte_offset = row_offset
                + static_cast<std::size_t>(mismatch.first - actual_row.begin());
            report_plane_mismatch(
                actual, expected, byte_offset, plane_index, plane, frame_description);
            return;
        }
    }
}

void expect_decodes_frame(
    mffv1::IDecoder& decoder,
    const mffv1_testvectors::DecodeVector& vector,
    std::size_t frame_index,
    std::span<const std::byte> frame_payload,
    std::span<const mffv1_testvectors::PlaneVector> expected_planes)
{
    SCOPED_TRACE(frame_index);
    ASSERT_FALSE(frame_payload.empty());
    ASSERT_FALSE(expected_planes.empty());

    mffv1::FrameInfo info;
    const auto inspect_status = decoder.inspect_frame(frame_payload, info);
    ASSERT_TRUE(inspect_status.ok()) << inspect_status.message;
    EXPECT_EQ(info.width, vector.frame_width);
    EXPECT_EQ(info.height, vector.frame_height);
    ASSERT_EQ(info.plane_count, expected_planes.size());

    std::vector<std::vector<std::byte>> plane_storage;
    std::vector<mffv1::MutablePlaneView> output_planes;
    plane_storage.reserve(expected_planes.size());
    output_planes.reserve(expected_planes.size());

    for (std::size_t index = 0; index < expected_planes.size(); ++index) {
        const auto& expected = expected_planes[index];
        SCOPED_TRACE(index);
        std::size_t expected_size = 0;
        ASSERT_TRUE(compute_plane_size(expected, expected_size));
        EXPECT_EQ(info.planes[index].role, expected.info.role);
        EXPECT_EQ(info.planes[index].sample_format, expected.info.sample_format);
        EXPECT_EQ(info.planes[index].width, expected.info.width);
        EXPECT_EQ(info.planes[index].height, expected.info.height);
        EXPECT_LE(info.planes[index].stride_bytes, expected.info.stride_bytes);

        plane_storage.emplace_back(expected_size, std::byte{0xa5});
        output_planes.push_back(
            mffv1::MutablePlaneView{plane_storage.back().data(), expected.info});
    }

    mffv1::MutableFrameView output{output_planes.data(), output_planes.size()};
    const auto status = decoder.decode_frame(frame_payload, output);
    const auto frame_description = describe_frame_parse(vector, frame_payload);
    if (!status.ok()) {
        for (std::size_t index = 0; index < expected_planes.size(); ++index) {
            const bool plane_was_touched = std::any_of(
                plane_storage[index].begin(),
                plane_storage[index].end(),
                [](std::byte value) { return value != std::byte{0xa5}; });
            if (!plane_was_touched) {
                continue;
            }
            const auto& expected = expected_planes[index];
            std::size_t expected_size = 0;
            ASSERT_TRUE(compute_plane_size(expected, expected_size));
            const std::vector<std::byte> expected_bytes{
                expected.samples.begin(), expected.samples.begin() + expected_size};
            expect_plane_matches(
                plane_storage[index], expected_bytes, index, expected.info, frame_description);
        }
    }
    ASSERT_TRUE(status.ok()) << describe_status(status) << "\n"
                             << describe_golomb_rice_partial_decode(
                                    vector, frame_payload, expected_planes)
                             << "\n"
                             << frame_description;

    for (std::size_t index = 0; index < expected_planes.size(); ++index) {
        const auto& expected = expected_planes[index];
        std::size_t expected_size = 0;
        ASSERT_TRUE(compute_plane_size(expected, expected_size));
        const std::vector<std::byte> expected_bytes{
            expected.samples.begin(), expected.samples.begin() + expected_size};
        expect_plane_matches(
            plane_storage[index], expected_bytes, index, expected.info, frame_description);
    }
}

void expect_decodes_vector(const mffv1_testvectors::DecodeVector& vector)
{
    SCOPED_TRACE(vector.name);
    ASSERT_GT(vector.frame_width, 0u);
    ASSERT_GT(vector.frame_height, 0u);
    ASSERT_FALSE(vector.configuration_record.empty());
    ASSERT_FALSE(vector.frame_payloads.empty());
    ASSERT_FALSE(vector.expected_planes.empty());
    ASSERT_EQ(vector.frame_payloads.size(), vector.expected_planes.size());

    mffv1::DecoderOptions options;
    options.frame_width = vector.frame_width;
    options.frame_height = vector.frame_height;
    auto decoder = mffv1::create_decoder(options);
    ASSERT_TRUE(decoder.status.ok()) << decoder.status.message;
    ASSERT_NE(decoder.decoder, nullptr);
    const auto configure_status =
        decoder.decoder->configure(vector.configuration_record);
    ASSERT_TRUE(configure_status.ok()) << describe_status(configure_status);

    for (std::size_t frame_index = 0;
         frame_index < vector.frame_payloads.size();
         ++frame_index) {
        expect_decodes_frame(
            *decoder.decoder,
            vector,
            frame_index,
            vector.frame_payloads[frame_index],
            vector.expected_planes[frame_index]);
    }
}

std::string current_test_vector_filter()
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, "MFFV1_TEST_VECTOR_FILTER") != 0
        || value == nullptr) {
        return {};
    }
    std::string filter{value};
    std::free(value);
    return filter;
#else
    const auto* filter = std::getenv("MFFV1_TEST_VECTOR_FILTER");
    return filter == nullptr ? std::string{} : std::string{filter};
#endif
}

bool matches_test_vector_filter(std::string_view name, std::string_view filter)
{
    return filter.empty() || name.find(filter) != std::string_view::npos;
}

} // namespace
#endif

TEST(TestVectorTest, GeneratedVectorsAreAvailable)
{
#if defined(NO_DEFINE_TEST_VECTOR_DATA)
    GTEST_SKIP() << "external FFV1 test vectors have not been generated";
#else
    ASSERT_FALSE(mffv1_testvectors::decode_vectors().empty());
#endif
}

TEST(TestVectorTest, GeneratedVectorsDecodeThroughPublicApi)
{
#if defined(NO_DEFINE_TEST_VECTOR_DATA)
    GTEST_SKIP() << "external FFV1 test vectors have not been generated";
#else
    const auto filter = current_test_vector_filter();
    for (const auto& vector : mffv1_testvectors::decode_vectors()) {
        if (!matches_test_vector_filter(vector.name, filter)) {
            continue;
        }
        expect_decodes_vector(vector);
    }
#endif
}
