#include "test_vector_data.hpp"

#include "codec/configuration_record_parser.hpp"
#include "codec/frame_parser.hpp"
#include "codec/legacy_frame_bootstrap_parser.hpp"
#include "codec/slice_decoder.hpp"
#include "codec/slice_header_parser.hpp"
#include "codec/slice_output_window.hpp"
#include "entropy/range_coder.hpp"
#include "mffv1/color_transform.hpp"
#include "mffv1/configuration_parser.hpp"
#include "mffv1/context_model.hpp"
#include "mffv1/codec.hpp"
#include "mffv1/line_state.hpp"
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

std::string describe_legacy_golomb_rice_boundary_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap);

std::string describe_legacy_range_expected_residual_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap);

std::string describe_legacy_range_slice_header_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap);

std::uint32_t read_sample(std::span<const std::byte> bytes,
                          std::size_t sample_offset,
                          mffv1::SampleFormat format);

std::uint32_t sample_at(std::span<const std::byte> bytes,
                        const mffv1::PlaneInfo& plane,
                        std::uint32_t x,
                        std::uint32_t y);

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

std::string describe_range_state(
    const mffv1::entropy::RangeCoder::ArithmeticState& state)
{
    std::ostringstream out;
    out << "range=0x" << std::hex << state.range
        << " low=0x" << state.low << std::dec
        << " byte=" << state.byte_position
        << " end=" << state.end
        << " init=" << state.initialized;
    return out.str();
}

std::string describe_payload_bytes(
    std::span<const std::byte> payload,
    std::uint64_t center_offset,
    std::size_t before,
    std::size_t count,
    std::string_view label)
{
    if (payload.empty()) {
        return {};
    }
    const auto start = center_offset > before
        ? static_cast<std::size_t>(center_offset - before)
        : std::size_t{0};
    if (start >= payload.size()) {
        return {};
    }
    const auto end = std::min(payload.size(), start + count);
    std::ostringstream out;
    out << " " << label << "@" << start << "=";
    for (std::size_t i = start; i < end; ++i) {
        if (i != start) {
            out << ",";
        }
        out << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(payload[i])
            << std::dec << std::setfill(' ');
    }
    return out.str();
}

std::string describe_stream_summary(const mffv1::syntax::StreamParameters& stream)
{
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
    return out.str();
}

std::string describe_legacy_range_parameter_trace(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::syntax::StreamParameters& stream,
    std::string_view label)
{
    if (stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()) {
        return {};
    }

    mffv1::entropy::RangeCoder reader;
    auto status = reader.reset(vector.frame_payloads.front());
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }
    bool keyframe = false;
    status = reader.read_bool(keyframe);
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }
    if (!keyframe) {
        return std::string{" "} + std::string{label} + "=non-keyframe";
    }

    const std::array<std::size_t, 1> parameter_context_counts{1};
    status = reader.reconfigure_contexts(parameter_context_counts);
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }

    std::ostringstream out;
    out << " " << label;
    const auto append_named_state = [&out, &reader](std::string_view name) {
        out << " " << name << "{"
            << describe_range_state(reader.arithmetic_state())
            << "}";
    };
    const auto append_inline_state = [&out, &reader]() {
        out << "{"
            << describe_range_state(reader.arithmetic_state())
            << "}";
    };
    const auto read_u = [&reader, &status, &out, &append_inline_state](
                            std::string_view name,
                            std::uint64_t& value) {
        status = reader.read_unsigned(value);
        if (!status.ok()) {
            out << " " << name << "=err(" << describe_status(status) << ")";
            return false;
        }
        out << " " << name << "=" << value;
        append_inline_state();
        return true;
    };
    const auto read_b = [&reader, &status, &out, &append_inline_state](
                            std::string_view name,
                            bool& value) {
        status = reader.read_bool(value);
        if (!status.ok()) {
            out << " " << name << "=err(" << describe_status(status) << ")";
            return false;
        }
        out << " " << name << "=" << value;
        append_inline_state();
        return true;
    };

    std::uint64_t value = 0;
    if (!read_u("version", value)) {
        return out.str();
    }
    const auto version = value;
    if (version >= 3 && !read_u("micro", value)) {
        return out.str();
    }
    if (!read_u("coder", value)) {
        return out.str();
    }
    if (value == 2) {
        for (std::size_t state = 1; state < 256; ++state) {
            std::int64_t delta = 0;
            status = reader.read_signed(delta);
            if (!status.ok()) {
                out << " state_delta[" << state << "]=err("
                    << describe_status(status) << ")";
                return out.str();
            }
        }
        append_named_state("state_transition");
    }
    if (!read_u("colorspace", value)) {
        return out.str();
    }
    if (version >= 1 && !read_u("bits", value)) {
        return out.str();
    }
    bool flag = false;
    if (!read_b("chroma", flag)
        || !read_u("hsub", value)
        || !read_u("vsub", value)
        || !read_b("extra", flag)) {
        return out.str();
    }
    if (version >= 3
        && (!read_u("h_slices_minus1", value)
            || !read_u("v_slices_minus1", value)
            || !read_u("qset_count", value))) {
        return out.str();
    }
    append_named_state("before_quant");
    return out.str();
}

std::string describe_legacy_range_symbol_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::syntax::StreamParameters& stream,
    bool reconfigure_content_contexts,
    std::string_view label)
{
    if (stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()
        || stream.quant_table_sets.empty()) {
        return {};
    }

    mffv1::entropy::RangeCoder reader;
    auto status = reader.reset(vector.frame_payloads.front());
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }
    bool keyframe = false;
    status = reader.read_bool(keyframe);
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }
    if (!keyframe) {
        return std::string{" "} + std::string{label} + "=non-keyframe";
    }

    const std::array<std::size_t, 1> parameter_context_counts{1};
    status = reader.reconfigure_contexts(parameter_context_counts);
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }
    mffv1::syntax::StreamParameters parsed_stream;
    const mffv1::syntax::ConfigurationParser parser;
    status = parser.parse(reader, parsed_stream);
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }
    status = reader.set_state_transition(stream.state_transition);
    if (!status.ok()) {
        return std::string{" "} + std::string{label} + "=" + describe_status(status);
    }

    if (reconfigure_content_contexts) {
        std::vector<std::size_t> context_counts;
        context_counts.reserve(stream.quant_table_sets.size());
        for (const auto& set : stream.quant_table_sets) {
            context_counts.push_back(set.context_count);
        }
        status = reader.reconfigure_contexts(context_counts);
        if (!status.ok()) {
            return std::string{" "} + std::string{label} + "=" + describe_status(status);
        }
    }

    std::ostringstream out;
    out << " " << label << " diffs=";
    for (std::size_t i = 0; i < 8; ++i) {
        std::int64_t difference = 0;
        status = reader.read_signed(0, 0, difference);
        if (!status.ok()) {
            out << "err(" << describe_status(status) << ")";
            break;
        }
        if (i != 0) {
            out << ",";
        }
        out << difference;
    }
    out << " after_probe{"
        << describe_range_state(reader.arithmetic_state())
        << "}";
    return out.str();
}

std::string describe_legacy_range_reset_boundary_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    const auto& stream = bootstrap.stream;
    if (stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()
        || stream.quant_table_sets.empty()
        || bootstrap.content_byte_offset == 0) {
        return {};
    }

    const auto payload = vector.frame_payloads.front();
    std::vector<std::size_t> context_counts;
    context_counts.reserve(stream.quant_table_sets.size());
    for (const auto& set : stream.quant_table_sets) {
        context_counts.push_back(set.context_count);
    }

    const auto center = bootstrap.content_byte_offset;
    const auto begin = center > 4 ? center - 4 : std::uint64_t{0};
    const auto end = std::min<std::uint64_t>(
        center + 24,
        payload.size() > 2 ? static_cast<std::uint64_t>(payload.size() - 2) : 0);

    std::ostringstream out;
    out << " reset_boundary_probe";
    for (auto offset = begin; offset <= end; ++offset) {
        const auto local_payload = payload.subspan(static_cast<std::size_t>(offset));
        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset(local_payload,
                                   context_counts,
                                   {},
                                   stream.state_transition);
        out << " @" << offset << "=";
        if (!status.ok()) {
            out << "err(" << describe_status(status) << ")";
            continue;
        }

        out << "[";
        for (std::size_t i = 0; i < 4; ++i) {
            std::int64_t difference = 0;
            status = reader.read_signed(0, 0, difference);
            if (!status.ok()) {
                out << "err(" << describe_status(status) << ")";
                break;
            }
            if (i != 0) {
                out << ",";
            }
            out << difference;
        }
        out << "]";
    }
    return out.str();
}

std::string describe_legacy_range_shifted_state_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    const auto& stream = bootstrap.stream;
    if (stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()
        || stream.quant_table_sets.empty()
        || bootstrap.content_byte_offset == 0) {
        return {};
    }

    const auto payload = vector.frame_payloads.front();
    std::vector<std::size_t> context_counts;
    context_counts.reserve(stream.quant_table_sets.size());
    for (const auto& set : stream.quant_table_sets) {
        context_counts.push_back(set.context_count);
    }

    const auto center = bootstrap.content_byte_offset;
    const auto begin = center > 2 ? center - 2 : std::uint64_t{0};
    const auto end = std::min<std::uint64_t>(
        center + 2,
        static_cast<std::uint64_t>(payload.size()));

    std::ostringstream out;
    out << " shifted_state_probe";
    for (auto offset = begin; offset <= end; ++offset) {
        auto state = bootstrap.range_state_after_parameters;
        state.byte_position = offset;
        state.end = offset >= payload.size();
        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset_from_arithmetic_state(
            payload,
            context_counts,
            {},
            stream.state_transition,
            state);
        out << " @" << offset << "=";
        if (!status.ok()) {
            out << "err(" << describe_status(status) << ")";
            continue;
        }

        out << "[";
        for (std::size_t i = 0; i < 4; ++i) {
            std::int64_t difference = 0;
            status = reader.read_signed(0, 0, difference);
            if (!status.ok()) {
                out << "err(" << describe_status(status) << ")";
                break;
            }
            if (i != 0) {
                out << ",";
            }
            out << difference;
        }
        out << "]";
    }
    return out.str();
}

std::string describe_legacy_range_initial_state_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    const auto& stream = bootstrap.stream;
    if (stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()
        || stream.quant_table_sets.size() != 1
        || stream.quant_table_sets.front().context_count != 1) {
        return {};
    }

    const auto payload = vector.frame_payloads.front();
    constexpr std::array<std::size_t, 1> context_counts{1};
    struct CandidateMatch {
        std::uint16_t state = 0;
        std::size_t matched_samples = 0;
        std::uint32_t mismatch_x = 0;
        std::uint32_t mismatch_y = 0;
        bool failed = false;
    };
    enum class InitialStatePattern {
        Uniform,
        ZeroOnly,
        FrozenZero,
        ExponentOnly,
        SignOnly,
        MagnitudeOnly,
    };

    std::size_t measured_sample_count = 0;
    if (!vector.expected_planes.empty()
        && !vector.expected_planes.front().empty()) {
        const auto& measured = vector.expected_planes.front().front();
        measured_sample_count = static_cast<std::size_t>(measured.info.width)
            * static_cast<std::size_t>(measured.info.height);
    }
    const auto make_swapped_transition = [](
                                             const mffv1::syntax::StateTransitionTable& transition) {
        mffv1::syntax::StateTransitionTable swapped{};
        for (std::size_t state = 0; state < swapped.size(); ++state) {
            swapped[state] = static_cast<std::uint8_t>(
                256u - transition[(256u - state) & 0xffu]);
        }
        return swapped;
    };
    const auto swapped_custom_transition = make_swapped_transition(stream.state_transition);

    const auto measure_candidate = [&](
                                       std::uint16_t candidate,
                                       const mffv1::syntax::StateTransitionTable& transition,
                                       InitialStatePattern pattern,
                                       CandidateMatch& out_match) {
        out_match.state = candidate;
        if (vector.expected_planes.empty()
            || vector.expected_planes.front().empty()
            || stream.quant_table_sets.empty()) {
            return false;
        }

        const auto& expected = vector.expected_planes.front().front();
        if (expected.info.width == 0 || expected.info.height == 0
            || expected.info.sample_format != mffv1::SampleFormat::UInt8) {
            return false;
        }

        mffv1::entropy::RangeCoder::ScalarContextStates states{};
        const auto candidate_state = static_cast<std::uint8_t>(candidate);
        states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
        const auto fill_range = [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                states[i] = candidate_state;
            }
        };
        switch (pattern) {
        case InitialStatePattern::Uniform:
            states.fill(candidate_state);
            break;
        case InitialStatePattern::ZeroOnly:
            states[0] = candidate_state;
            break;
        case InitialStatePattern::FrozenZero:
            states[0] = candidate_state;
            break;
        case InitialStatePattern::ExponentOnly:
            fill_range(1, 11);
            break;
        case InitialStatePattern::SignOnly:
            fill_range(11, 22);
            break;
        case InitialStatePattern::MagnitudeOnly:
            fill_range(22, states.size());
            break;
        }
        const std::array state_bank{std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{
            &states,
            1,
        }};

        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset_from_arithmetic_state(
            payload,
            context_counts,
            state_bank,
            transition,
            bootstrap.range_state_after_parameters);
        if (!status.ok()) {
            out_match.failed = true;
            return true;
        }

        const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
        mffv1::syntax::LineState line;
        status = line.reset(expected.info.width);
        if (!status.ok()) {
            out_match.failed = true;
            return true;
        }

        for (std::uint32_t y = 0; y < expected.info.height; ++y) {
            for (std::uint32_t x = 0; x < expected.info.width; ++x) {
                const auto neighbors = line.neighbors(x);
                const auto prediction = mffv1::syntax::Predictor::median_predict(
                    neighbors.left,
                    neighbors.top,
                    neighbors.top_left);
                mffv1::syntax::ContextDecision context;
                status = context_model.derive_context(neighbors, context);
                if (!status.ok()) {
                    out_match.failed = true;
                    return true;
                }
                std::int64_t difference64 = 0;
                status = reader.read_signed(0, context.context, difference64);
                if (!status.ok()) {
                    out_match.failed = true;
                    return true;
                }
                if (pattern == InitialStatePattern::FrozenZero) {
                    mffv1::entropy::RangeCoder::ContextStateBanks copied_contexts;
                    status = reader.copy_contexts(copied_contexts);
                    if (!status.ok() || copied_contexts.empty()
                        || copied_contexts.front().empty()) {
                        out_match.failed = true;
                        return true;
                    }
                    copied_contexts.front().front()[0] = candidate_state;
                    const std::array copied_state_banks{
                        std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{
                            copied_contexts.front().data(),
                            copied_contexts.front().size(),
                        },
                    };
                    status = reader.reset_from_arithmetic_state(
                        payload,
                        context_counts,
                        copied_state_banks,
                        transition,
                        reader.arithmetic_state());
                    if (!status.ok()) {
                        out_match.failed = true;
                        return true;
                    }
                }
                if (context.invert_difference) {
                    difference64 = -difference64;
                }
                const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                    prediction,
                    static_cast<std::int32_t>(difference64),
                    stream.bits_per_raw_sample);
                const auto expected_sample = sample_at(
                    expected.samples,
                    expected.info,
                    x,
                    y);
                if (static_cast<std::uint32_t>(reconstructed) != expected_sample) {
                    out_match.mismatch_x = x;
                    out_match.mismatch_y = y;
                    return true;
                }
                line.mutable_current()[x] = reconstructed;
                ++out_match.matched_samples;
            }
            line.swap_lines();
        }
        return true;
    };

    const auto append_best_matches = [&](
                                         std::ostringstream& out,
                                         std::string_view label,
                                         const mffv1::syntax::StateTransitionTable& transition,
                                         InitialStatePattern pattern) {
        std::vector<CandidateMatch> best_matches;
        best_matches.reserve(8);
        for (std::uint16_t candidate = 0; candidate <= 255; ++candidate) {
            CandidateMatch match;
            if (!measure_candidate(candidate, transition, pattern, match)) {
                continue;
            }
            best_matches.push_back(match);
            std::sort(best_matches.begin(),
                      best_matches.end(),
                      [](const CandidateMatch& lhs, const CandidateMatch& rhs) {
                          if (lhs.matched_samples != rhs.matched_samples) {
                              return lhs.matched_samples > rhs.matched_samples;
                          }
                          return lhs.state < rhs.state;
                      });
            if (best_matches.size() > 8) {
                best_matches.pop_back();
            }
        }
        if (best_matches.empty()) {
            return;
        }
        out << " " << label << "/" << measured_sample_count;
        for (const auto& match : best_matches) {
            out << " state" << match.state
                << "=" << match.matched_samples;
            if (match.failed) {
                out << "/fail";
            } else {
                out << "@" << match.mismatch_x << "," << match.mismatch_y;
            }
        }
    };

    const auto append_zero_state_trace = [&](
                                             std::ostringstream& out,
                                             std::uint8_t zero_state) {
        if (vector.expected_planes.empty()
            || vector.expected_planes.front().empty()
            || stream.quant_table_sets.empty()) {
            return;
        }
        const auto& expected = vector.expected_planes.front().front();
        if (expected.info.width == 0 || expected.info.height == 0
            || expected.info.sample_format != mffv1::SampleFormat::UInt8) {
            return;
        }

        mffv1::entropy::RangeCoder::ScalarContextStates states{};
        states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
        states[0] = zero_state;
        const std::array state_bank{std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{
            &states,
            1,
        }};
        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset_from_arithmetic_state(
            payload,
            context_counts,
            state_bank,
            stream.state_transition,
            bootstrap.range_state_after_parameters);
        if (!status.ok()) {
            out << " zero_state_trace=err(" << describe_status(status) << ")";
            return;
        }

        constexpr std::array<std::size_t, 12> sample_points{
            0, 1, 2, 3, 4, 8, 16, 64, 128, 256, 407, 408};
        std::size_t next_point = 0;
        std::size_t decoded_samples = 0;
        bool emitted_prefix = false;
        const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
        mffv1::syntax::LineState line;
        status = line.reset(expected.info.width);
        if (!status.ok()) {
            out << " zero_state_trace=err(" << describe_status(status) << ")";
            return;
        }
        const auto append_split = [](
                                      std::ostringstream& out,
                                      const mffv1::entropy::RangeCoder::ArithmeticState& state,
                                      std::uint8_t zero_state) {
            const auto product = static_cast<std::uint64_t>(state.range)
                * static_cast<std::uint64_t>(zero_state);
            const auto current_zero_span = static_cast<std::uint32_t>(product >> 8);
            const auto current_nonzero_span = state.range - current_zero_span;
            const auto ceil_zero_span = static_cast<std::uint32_t>((product + 255u) >> 8);
            const auto ceil_nonzero_span = state.range - ceil_zero_span;
            const auto midpoint_zero_span = static_cast<std::uint32_t>((product + 128u) >> 8);
            const auto midpoint_nonzero_span = state.range - midpoint_zero_span;
            const auto needed_state = state.low < current_nonzero_span
                ? (static_cast<std::uint64_t>(state.range - state.low)
                   * 256u + state.range - 1u)
                    / static_cast<std::uint64_t>(state.range)
                : 0u;
            out << " split_nonzero/zero="
                << current_nonzero_span << "/" << current_zero_span
                << " ceil_nonzero=" << ceil_nonzero_span
                << " mid_nonzero=" << midpoint_nonzero_span
                << " need_state=" << needed_state;
        };

        for (std::uint32_t y = 0; y < expected.info.height; ++y) {
            for (std::uint32_t x = 0; x < expected.info.width; ++x) {
                const auto neighbors = line.neighbors(x);
                const auto prediction = mffv1::syntax::Predictor::median_predict(
                    neighbors.left,
                    neighbors.top,
                    neighbors.top_left);
                mffv1::syntax::ContextDecision context;
                status = context_model.derive_context(neighbors, context);
                if (!status.ok()) {
                    out << " zero_state_trace=err(" << describe_status(status) << ")";
                    return;
                }
                const auto before_symbol_state = reader.arithmetic_state();
                std::int64_t difference64 = 0;
                status = reader.read_signed(0, context.context, difference64);
                if (!status.ok()) {
                    out << " zero_state_trace=err(" << describe_status(status) << ")";
                    return;
                }
                mffv1::entropy::RangeCoder::ContextStateBanks copied_contexts;
                status = reader.copy_contexts(copied_contexts);
                if (!status.ok() || copied_contexts.empty()
                    || copied_contexts.front().empty()) {
                    out << " zero_state_trace=context_err";
                    return;
                }
                while (next_point < sample_points.size()
                       && sample_points[next_point] == decoded_samples) {
                    if (!emitted_prefix) {
                        out << " zero_state_trace";
                        emitted_prefix = true;
                    }
                    out << " #" << decoded_samples
                        << "=" << static_cast<int>(copied_contexts.front().front()[0]);
                    if (decoded_samples == 407 || decoded_samples == 408) {
                        out << "{before:"
                            << describe_range_state(before_symbol_state)
                            << " after:"
                            << describe_range_state(reader.arithmetic_state())
                            << " diff=" << difference64;
                        append_split(out, before_symbol_state, zero_state);
                        out << "}";
                    }
                    ++next_point;
                }
                if (context.invert_difference) {
                    difference64 = -difference64;
                }
                const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                    prediction,
                    static_cast<std::int32_t>(difference64),
                    stream.bits_per_raw_sample);
                const auto expected_sample = sample_at(
                    expected.samples,
                    expected.info,
                    x,
                    y);
                if (static_cast<std::uint32_t>(reconstructed) != expected_sample) {
                    if (!emitted_prefix) {
                        out << " zero_state_trace";
                    }
                    out << " mismatch=" << decoded_samples
                        << "@" << x << "," << y
                        << " state0="
                        << static_cast<int>(copied_contexts.front().front()[0]);
                    append_split(out, before_symbol_state, zero_state);
                    out << " before{"
                        << describe_range_state(before_symbol_state)
                        << "} after{"
                        << describe_range_state(reader.arithmetic_state())
                        << "} diff=" << difference64;
                    return;
                }
                line.mutable_current()[x] = reconstructed;
                ++decoded_samples;
            }
            line.swap_lines();
        }
    };

    const auto append_pivot_low_probe = [&](
                                            std::ostringstream& out,
                                            std::uint8_t zero_state) {
        if (vector.expected_planes.empty()
            || vector.expected_planes.front().empty()
            || stream.quant_table_sets.empty()) {
            return;
        }
        const auto& expected = vector.expected_planes.front().front();
        if (expected.info.width == 0 || expected.info.height == 0
            || expected.info.sample_format != mffv1::SampleFormat::UInt8) {
            return;
        }

        mffv1::entropy::RangeCoder::ScalarContextStates states{};
        states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
        states[0] = zero_state;
        const std::array state_bank{std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{
            &states,
            1,
        }};
        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset_from_arithmetic_state(
            payload,
            context_counts,
            state_bank,
            stream.state_transition,
            bootstrap.range_state_after_parameters);
        if (!status.ok()) {
            out << " pivot_low_probe=err(" << describe_status(status) << ")";
            return;
        }

        constexpr std::size_t kPivotSample = 408;
        std::size_t decoded_samples = 0;
        const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
        mffv1::syntax::LineState line;
        status = line.reset(expected.info.width);
        if (!status.ok()) {
            out << " pivot_low_probe=err(" << describe_status(status) << ")";
            return;
        }

        for (std::uint32_t y = 0; y < expected.info.height; ++y) {
            for (std::uint32_t x = 0; x < expected.info.width; ++x) {
                const auto neighbors = line.neighbors(x);
                const auto prediction = mffv1::syntax::Predictor::median_predict(
                    neighbors.left,
                    neighbors.top,
                    neighbors.top_left);
                mffv1::syntax::ContextDecision context;
                status = context_model.derive_context(neighbors, context);
                if (!status.ok()) {
                    out << " pivot_low_probe=err(" << describe_status(status) << ")";
                    return;
                }
                if (decoded_samples == kPivotSample) {
                    mffv1::entropy::RangeCoder::ContextStateBanks copied_contexts;
                    status = reader.copy_contexts(copied_contexts);
                    if (!status.ok() || copied_contexts.empty()
                        || copied_contexts.front().empty()) {
                        out << " pivot_low_probe=context_err";
                        return;
                    }
                    const std::array copied_state_banks{
                        std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{
                            copied_contexts.front().data(),
                            copied_contexts.front().size(),
                        },
                    };
                    const auto base_state = reader.arithmetic_state();
                    out << " pivot_low_probe base{"
                        << describe_range_state(base_state) << "}";
                    constexpr std::array<std::uint32_t, 12> low_values{
                        0, 1, 2, 8, 12, 13, 14, 15, 16, 31, 64, 128};
                    for (const auto low : low_values) {
                        if (low >= base_state.range) {
                            continue;
                        }
                        auto mutated_state = base_state;
                        mutated_state.low = low;
                        mffv1::entropy::RangeCoder probe_reader;
                        status = probe_reader.reset_from_arithmetic_state(
                            payload,
                            context_counts,
                            copied_state_banks,
                            stream.state_transition,
                            mutated_state);
                        if (!status.ok()) {
                            out << " low" << low
                                << "=err(" << describe_status(status) << ")";
                            continue;
                        }
                        std::int64_t difference64 = 0;
                        status = probe_reader.read_signed(0, context.context, difference64);
                        if (!status.ok()) {
                            out << " low" << low
                                << "=err(" << describe_status(status) << ")";
                            continue;
                        }
                        if (context.invert_difference) {
                            difference64 = -difference64;
                        }
                        const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                            prediction,
                            static_cast<std::int32_t>(difference64),
                            stream.bits_per_raw_sample);
                        const auto expected_sample = sample_at(
                            expected.samples,
                            expected.info,
                            x,
                            y);
                        out << " low" << low
                            << "=" << difference64
                            << "/" << reconstructed
                            << (static_cast<std::uint32_t>(reconstructed) == expected_sample
                                    ? ":ok"
                                    : ":bad");
                    }
                    return;
                }

                std::int64_t difference64 = 0;
                status = reader.read_signed(0, context.context, difference64);
                if (!status.ok()) {
                    out << " pivot_low_probe=err(" << describe_status(status) << ")";
                    return;
                }
                if (context.invert_difference) {
                    difference64 = -difference64;
                }
                const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                    prediction,
                    static_cast<std::int32_t>(difference64),
                    stream.bits_per_raw_sample);
                line.mutable_current()[x] = reconstructed;
                ++decoded_samples;
            }
            line.swap_lines();
        }
    };

    const auto append_refill_variant_probe = [&](
                                                 std::ostringstream& out,
                                                 std::uint8_t zero_state) {
        if (vector.expected_planes.empty()
            || vector.expected_planes.front().empty()
            || stream.quant_table_sets.empty()) {
            return;
        }
        const auto& expected = vector.expected_planes.front().front();
        if (expected.info.width == 0 || expected.info.height == 0
            || expected.info.sample_format != mffv1::SampleFormat::UInt8) {
            return;
        }

        struct RefillVariant {
            std::string_view label;
            std::uint32_t threshold = 256;
            std::int32_t byte_bias = 0;
            std::uint32_t split_bias = 0;
            bool inclusive_nonzero = false;
            std::uint32_t low_bias_after_symbol = 0;
            std::uint32_t low_bias_after_rac = 0;
            std::uint32_t low_bias_after_zero_rac = 0;
            std::uint32_t low_bias_after_one_rac = 0;
            std::uint32_t low_bias_after_one_subtract = 0;
            std::uint32_t low_bias_after_one_range_assign = 0;
            std::uint32_t low_bias_after_one_mode = 0;
            bool skip_zero_flag = false;
        };
        struct MiniRangeReader {
            std::span<const std::byte> payload;
            const mffv1::syntax::StateTransitionTable* transition = nullptr;
            std::uint32_t range = 0;
            std::uint32_t low = 0;
            std::uint64_t byte_position = 0;
            bool end = false;
            std::uint32_t refill_threshold = 256;
            std::int32_t byte_bias = 0;
            std::uint32_t split_bias = 0;
            bool inclusive_nonzero = false;
            std::uint32_t low_bias_after_symbol = 0;
            std::uint32_t low_bias_after_rac = 0;
            std::uint32_t low_bias_after_zero_rac = 0;
            std::uint32_t low_bias_after_one_rac = 0;
            std::uint32_t low_bias_after_one_subtract = 0;
            std::uint32_t low_bias_after_one_range_assign = 0;
            std::uint32_t low_bias_after_one_mode = 0;
            bool skip_zero_flag = false;
            mffv1::entropy::RangeCoder::ScalarContextStates states{};

            void refill() noexcept
            {
                if (range >= refill_threshold) {
                    return;
                }
                range <<= 8;
                low <<= 8;
                if (end) {
                    return;
                }
                if (byte_position < payload.size()) {
                    const auto payload_byte = static_cast<std::uint32_t>(
                        payload[static_cast<std::size_t>(byte_position)]);
                    const auto biased_byte = static_cast<std::uint32_t>(
                        std::clamp<std::int32_t>(
                            static_cast<std::int32_t>(payload_byte) + byte_bias,
                            0,
                            256));
                    low += biased_byte;
                    ++byte_position;
                }
                if (byte_position >= payload.size()) {
                    end = true;
                }
            }

            bool read_rac(std::uint8_t& state) noexcept
            {
                const auto pre_range = range;
                const auto product =
                    static_cast<std::uint64_t>(pre_range) * state + split_bias;
                const std::uint32_t rangeoff =
                    static_cast<std::uint32_t>(product >> 8);
                const auto nonzero_product =
                    static_cast<std::uint64_t>(pre_range) * (256u - state)
                    + (255u - split_bias);
                range -= rangeoff;
                const auto nonzero_span = range;
                const bool is_nonzero_path = inclusive_nonzero
                    ? low <= range
                    : low < range;
                if (is_nonzero_path) {
                    state = mffv1::syntax::range_zero_state(*transition, state);
                    refill();
                    low += low_bias_after_rac + low_bias_after_zero_rac;
                    return false;
                }
                low -= range;
                low += low_bias_after_one_subtract;
                range = rangeoff;
                low += low_bias_after_one_range_assign;
                state = mffv1::syntax::range_one_state(*transition, state);
                refill();
                switch (low_bias_after_one_mode) {
                case 1:
                    low += (product & 0xffu) != 0 ? 1u : 0u;
                    break;
                case 2:
                    low += static_cast<std::uint32_t>(product & 1u);
                    break;
                case 3:
                    low += static_cast<std::uint32_t>(pre_range & 1u);
                    break;
                case 4:
                    low += static_cast<std::uint32_t>(rangeoff & 1u);
                    break;
                case 5:
                    low += ((product & 0xffu) != 0 ? 1u : 0u)
                        + static_cast<std::uint32_t>(rangeoff & 1u);
                    break;
                case 6:
                    low += (nonzero_product & 0xffu) != 0 ? 1u : 0u;
                    break;
                case 7:
                    low += static_cast<std::uint32_t>(nonzero_product & 1u);
                    break;
                case 8:
                    low += static_cast<std::uint32_t>(nonzero_span & 1u);
                    break;
                case 9:
                    low += ((nonzero_product & 0xffu) != 0 ? 1u : 0u)
                        + static_cast<std::uint32_t>(nonzero_span & 1u);
                    break;
                case 10:
                    low += ((product & 0xffu) != 0 ? 1u : 0u)
                        + ((nonzero_product & 0xffu) != 0 ? 1u : 0u);
                    break;
                case 11:
                    low += static_cast<std::uint32_t>(rangeoff & 1u)
                        + static_cast<std::uint32_t>(nonzero_span & 1u);
                    break;
                case 12:
                    low += ((product & 0xffu) != 0 ? 1u : 0u)
                        + static_cast<std::uint32_t>(rangeoff & 1u)
                        + static_cast<std::uint32_t>(nonzero_span & 1u);
                    break;
                default:
                    break;
                }
                low += low_bias_after_rac + low_bias_after_one_rac;
                return true;
            }

            bool read_signed(std::int64_t& out_value) noexcept
            {
                bool bit = false;
                if (!skip_zero_flag) {
                    bit = read_rac(states[0]);
                    if (bit) {
                        out_value = 0;
                        low += low_bias_after_symbol;
                        return true;
                    }
                }

                std::uint32_t exponent = 0;
                for (;;) {
                    bit = read_rac(states[1 + std::min<std::uint32_t>(exponent, 9)]);
                    if (!bit) {
                        break;
                    }
                    ++exponent;
                    if (exponent > 62) {
                        return false;
                    }
                }

                std::uint64_t magnitude = 1;
                for (std::int32_t i = static_cast<std::int32_t>(exponent) - 1; i >= 0; --i) {
                    bit = read_rac(states[22 + std::min<std::int32_t>(i, 9)]);
                    magnitude = (magnitude << 1) | (bit ? 1u : 0u);
                }
                if (magnitude > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                    return false;
                }

                auto signed_value = static_cast<std::int64_t>(magnitude);
                bit = read_rac(states[11 + std::min<std::uint32_t>(exponent, 10)]);
                if (bit) {
                    signed_value = -signed_value;
                }
                out_value = signed_value;
                low += low_bias_after_symbol;
                return true;
            }
        };

        const auto measure_variant = [&](const RefillVariant& variant,
                                         std::uint8_t initial_zero_state) {
            CandidateMatch match;
            MiniRangeReader reader;
            reader.payload = payload;
            reader.transition = &stream.state_transition;
            reader.range = bootstrap.range_state_after_parameters.range;
            reader.low = bootstrap.range_state_after_parameters.low;
            reader.byte_position = bootstrap.range_state_after_parameters.byte_position;
            reader.end = bootstrap.range_state_after_parameters.end
                || reader.byte_position >= payload.size();
            reader.refill_threshold = variant.threshold;
            reader.byte_bias = variant.byte_bias;
            reader.split_bias = variant.split_bias;
            reader.inclusive_nonzero = variant.inclusive_nonzero;
            reader.low_bias_after_symbol = variant.low_bias_after_symbol;
            reader.low_bias_after_rac = variant.low_bias_after_rac;
            reader.low_bias_after_zero_rac = variant.low_bias_after_zero_rac;
            reader.low_bias_after_one_rac = variant.low_bias_after_one_rac;
            reader.low_bias_after_one_subtract = variant.low_bias_after_one_subtract;
            reader.low_bias_after_one_range_assign = variant.low_bias_after_one_range_assign;
            reader.low_bias_after_one_mode = variant.low_bias_after_one_mode;
            reader.skip_zero_flag = variant.skip_zero_flag;
            reader.states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
            reader.states[0] = initial_zero_state;

            const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
            mffv1::syntax::LineState line;
            auto status = line.reset(expected.info.width);
            if (!status.ok()) {
                match.failed = true;
                return match;
            }

            for (std::uint32_t y = 0; y < expected.info.height; ++y) {
                for (std::uint32_t x = 0; x < expected.info.width; ++x) {
                    const auto neighbors = line.neighbors(x);
                    const auto prediction = mffv1::syntax::Predictor::median_predict(
                        neighbors.left,
                        neighbors.top,
                        neighbors.top_left);
                    mffv1::syntax::ContextDecision context;
                    status = context_model.derive_context(neighbors, context);
                    if (!status.ok()) {
                        match.failed = true;
                        return match;
                    }
                    std::int64_t difference64 = 0;
                    if (!reader.read_signed(difference64)) {
                        match.failed = true;
                        return match;
                    }
                    if (context.invert_difference) {
                        difference64 = -difference64;
                    }
                    const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                        prediction,
                        static_cast<std::int32_t>(difference64),
                        stream.bits_per_raw_sample);
                    const auto expected_sample = sample_at(
                        expected.samples,
                        expected.info,
                        x,
                        y);
                    if (static_cast<std::uint32_t>(reconstructed) != expected_sample) {
                        match.mismatch_x = x;
                        match.mismatch_y = y;
                        return match;
                    }
                    line.mutable_current()[x] = reconstructed;
                    ++match.matched_samples;
                }
                line.swap_lines();
            }
            return match;
        };

        constexpr std::array<RefillVariant, 13> variants{{
            {"cur", 256, 0},
            {"plus1", 256, 1},
            {"minus1", 256, -1},
            {"th257", 257, 0},
            {"th512", 512, 0},
            {"th257p1", 257, 1},
            {"round", 256, 0, 128},
            {"ceil", 256, 0, 255},
            {"incl", 256, 0, 0, true},
            {"ceilincl", 256, 0, 255, true},
            {"ceilb1", 256, 0, 255, false, 1},
            {"ceilb2", 256, 0, 255, false, 2},
            {"ceilbothrem", 256, 0, 255, false, 0, 0, 0, 0, 0, 0, 10},
        }};
        out << " refill_variant_probe/" << measured_sample_count;
        for (const auto& variant : variants) {
            const auto match = measure_variant(variant, zero_state);
            out << " " << variant.label << "=" << match.matched_samples;
            if (match.failed) {
                out << "/fail";
            } else {
                out << "@" << match.mismatch_x << "," << match.mismatch_y;
            }
        }

        const auto append_top_match = [](std::vector<CandidateMatch>& best_matches,
                                         CandidateMatch match) {
            best_matches.push_back(match);
            std::sort(best_matches.begin(),
                      best_matches.end(),
                      [](const CandidateMatch& lhs, const CandidateMatch& rhs) {
                          if (lhs.matched_samples != rhs.matched_samples) {
                              return lhs.matched_samples > rhs.matched_samples;
                          }
                          return lhs.state < rhs.state;
                      });
            if (best_matches.size() > 8) {
                best_matches.pop_back();
            }
        };

        std::vector<CandidateMatch> split_bias_matches;
        split_bias_matches.reserve(8);
        for (std::uint16_t bias = 0; bias <= 255; ++bias) {
            const RefillVariant variant{"", 256, 0, static_cast<std::uint32_t>(bias)};
            auto match = measure_variant(variant, zero_state);
            match.state = bias;
            append_top_match(split_bias_matches, match);
        }
        out << " split_bias_best";
        for (const auto& match : split_bias_matches) {
            out << " bias" << match.state
                << "=" << match.matched_samples;
            if (match.failed) {
                out << "/fail";
            } else {
                out << "@" << match.mismatch_x << "," << match.mismatch_y;
            }
        }

        std::vector<CandidateMatch> ceil_zero_matches;
        ceil_zero_matches.reserve(8);
        const RefillVariant ceil_variant{"", 256, 0, 255};
        for (std::uint16_t candidate = 0; candidate <= 255; ++candidate) {
            auto match = measure_variant(ceil_variant, static_cast<std::uint8_t>(candidate));
            match.state = candidate;
            append_top_match(ceil_zero_matches, match);
        }
        out << " ceil_zero_best";
        for (const auto& match : ceil_zero_matches) {
            out << " state" << match.state
                << "=" << match.matched_samples;
            if (match.failed) {
                out << "/fail";
            } else {
                out << "@" << match.mismatch_x << "," << match.mismatch_y;
            }
        }

        std::vector<CandidateMatch> ceil_low_bias_matches;
        ceil_low_bias_matches.reserve(8);
        for (std::uint16_t bias = 0; bias <= 32; ++bias) {
            const RefillVariant variant{"",
                                        256,
                                        0,
                                        255,
                                        false,
                                        static_cast<std::uint32_t>(bias)};
            auto match = measure_variant(variant, zero_state);
            match.state = bias;
            append_top_match(ceil_low_bias_matches, match);
        }
        out << " ceil_low_bias_best";
        for (const auto& match : ceil_low_bias_matches) {
            out << " bias" << match.state
                << "=" << match.matched_samples;
            if (match.failed) {
                out << "/fail";
            } else {
                out << "@" << match.mismatch_x << "," << match.mismatch_y;
            }
        }

        const auto append_bias_family = [&](
                                            std::string_view label,
                                            auto configure_variant) {
            std::vector<CandidateMatch> matches;
            matches.reserve(8);
            for (std::uint16_t bias = 0; bias <= 8; ++bias) {
                RefillVariant variant{"", 256, 0, 255};
                configure_variant(variant, bias);
                auto match = measure_variant(variant, zero_state);
                match.state = bias;
                append_top_match(matches, match);
            }
            out << " " << label;
            for (const auto& match : matches) {
                out << " bias" << match.state
                    << "=" << match.matched_samples;
                if (match.failed) {
                    out << "/fail";
                } else {
                    out << "@" << match.mismatch_x << "," << match.mismatch_y;
                }
            }
        };
        append_bias_family("ceil_rac_bias_best",
                           [](RefillVariant& variant, std::uint16_t bias) {
                               variant.low_bias_after_rac = bias;
                           });
        append_bias_family("ceil_zero_rac_bias_best",
                           [](RefillVariant& variant, std::uint16_t bias) {
                               variant.low_bias_after_zero_rac = bias;
                           });
        append_bias_family("ceil_one_rac_bias_best",
                           [](RefillVariant& variant, std::uint16_t bias) {
                               variant.low_bias_after_one_rac = bias;
                           });
        append_bias_family("ceil_one_sub_bias_best",
                           [](RefillVariant& variant, std::uint16_t bias) {
                               variant.low_bias_after_one_subtract = bias;
                           });
        append_bias_family("ceil_one_range_bias_best",
                           [](RefillVariant& variant, std::uint16_t bias) {
                               variant.low_bias_after_one_range_assign = bias;
                           });
        constexpr std::array<std::string_view, 12> one_mode_labels{
            "rem",
            "prodlsb",
            "rangelsb",
            "rangeofflsb",
            "rem_rangeofflsb",
            "nonzerorem",
            "nonzeroprodlsb",
            "nonzerospanlsb",
            "nonzerorem_spanlsb",
            "bothrem",
            "bothspanlsb",
            "rem_bothspanlsb",
        };
        out << " ceil_one_mode_probe";
        for (std::uint32_t mode = 1; mode <= one_mode_labels.size(); ++mode) {
            RefillVariant variant{"", 256, 0, 255};
            variant.low_bias_after_one_mode = mode;
            const auto match = measure_variant(variant, zero_state);
            out << " " << one_mode_labels[mode - 1]
                << "=" << match.matched_samples;
            if (match.failed) {
                out << "/fail";
            } else {
                out << "@" << match.mismatch_x << "," << match.mismatch_y;
            }
        }
        RefillVariant nozero_variant{"", 256, 0, 255};
        nozero_variant.skip_zero_flag = true;
        const auto nozero_match = measure_variant(nozero_variant, zero_state);
        out << " nozero_flag_probe="
            << nozero_match.matched_samples;
        if (nozero_match.failed) {
            out << "/fail";
        } else {
            out << "@" << nozero_match.mismatch_x
                << "," << nozero_match.mismatch_y;
        }
        RefillVariant bothrem_variant{"", 256, 0, 255};
        bothrem_variant.low_bias_after_one_mode = 10;

        const auto append_mini_state = [](std::ostringstream& trace_out,
                                          const MiniRangeReader& reader) {
            trace_out << "range=0x" << std::hex << reader.range
                      << " low=0x" << reader.low << std::dec
                      << " byte=" << reader.byte_position
                      << " end=" << reader.end;
        };
        const auto append_variant_trace = [&](const RefillVariant& variant,
                                              std::uint8_t initial_zero_state,
                                              std::string_view label) {
            MiniRangeReader reader;
            reader.payload = payload;
            reader.transition = &stream.state_transition;
            reader.range = bootstrap.range_state_after_parameters.range;
            reader.low = bootstrap.range_state_after_parameters.low;
            reader.byte_position = bootstrap.range_state_after_parameters.byte_position;
            reader.end = bootstrap.range_state_after_parameters.end
                || reader.byte_position >= payload.size();
            reader.refill_threshold = variant.threshold;
            reader.byte_bias = variant.byte_bias;
            reader.split_bias = variant.split_bias;
            reader.inclusive_nonzero = variant.inclusive_nonzero;
            reader.low_bias_after_symbol = variant.low_bias_after_symbol;
            reader.low_bias_after_rac = variant.low_bias_after_rac;
            reader.low_bias_after_zero_rac = variant.low_bias_after_zero_rac;
            reader.low_bias_after_one_rac = variant.low_bias_after_one_rac;
            reader.low_bias_after_one_subtract = variant.low_bias_after_one_subtract;
            reader.low_bias_after_one_range_assign = variant.low_bias_after_one_range_assign;
            reader.low_bias_after_one_mode = variant.low_bias_after_one_mode;
            reader.states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
            reader.states[0] = initial_zero_state;

            constexpr std::array<std::size_t, 4> trace_points{407, 408, 422, 423};
            std::size_t next_point = 0;
            std::size_t decoded_samples = 0;
            const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
            mffv1::syntax::LineState line;
            auto status = line.reset(expected.info.width);
            if (!status.ok()) {
                out << " " << label << "_trace=err(" << describe_status(status) << ")";
                return;
            }

            out << " " << label << "_trace";
            for (std::uint32_t y = 0; y < expected.info.height; ++y) {
                for (std::uint32_t x = 0; x < expected.info.width; ++x) {
                    const auto neighbors = line.neighbors(x);
                    const auto prediction = mffv1::syntax::Predictor::median_predict(
                        neighbors.left,
                        neighbors.top,
                        neighbors.top_left);
                    mffv1::syntax::ContextDecision context;
                    status = context_model.derive_context(neighbors, context);
                    if (!status.ok()) {
                        out << " err(" << describe_status(status) << ")";
                        return;
                    }

                    const auto before_range = reader.range;
                    const auto before_low = reader.low;
                    const auto before_byte = reader.byte_position;
                    const auto before_end = reader.end;
                    const auto before_state0 = reader.states[0];
                    std::int64_t difference64 = 0;
                    if (!reader.read_signed(difference64)) {
                        out << " fail=" << decoded_samples
                            << "@" << x << "," << y;
                        return;
                    }
                    if (context.invert_difference) {
                        difference64 = -difference64;
                    }
                    const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                        prediction,
                        static_cast<std::int32_t>(difference64),
                        stream.bits_per_raw_sample);
                    const auto expected_sample = sample_at(
                        expected.samples,
                        expected.info,
                        x,
                        y);

                    while (next_point < trace_points.size()
                           && trace_points[next_point] == decoded_samples) {
                        out << " #" << decoded_samples
                            << " s0=" << static_cast<int>(before_state0)
                            << " before{range=0x" << std::hex << before_range
                            << " low=0x" << before_low << std::dec
                            << " byte=" << before_byte
                            << " end=" << before_end
                            << "} diff=" << difference64
                            << " after{";
                        append_mini_state(out, reader);
                        out << "} rec=" << reconstructed
                            << " exp=" << expected_sample;
                        ++next_point;
                    }

                    if (static_cast<std::uint32_t>(reconstructed) != expected_sample) {
                        out << " mismatch=" << decoded_samples
                            << "@" << x << "," << y
                            << " s0=" << static_cast<int>(reader.states[0]);
                        return;
                    }
                    line.mutable_current()[x] = reconstructed;
                    ++decoded_samples;
                }
                line.swap_lines();
            }
        };
        append_variant_trace(ceil_variant, zero_state, "ceil");
        append_variant_trace(bothrem_variant, zero_state, "bothrem");
    };

    std::ostringstream out;
    out << " initial_state_probe";
    std::size_t match_count = 0;
    constexpr std::size_t kProbeSampleCount = 16;
    for (std::uint16_t candidate = 0; candidate <= 255; ++candidate) {
        mffv1::entropy::RangeCoder::ScalarContextStates states{};
        states.fill(static_cast<std::uint8_t>(candidate));
        const std::array state_bank{std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{
            &states,
            1,
        }};

        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset_from_arithmetic_state(
            payload,
            context_counts,
            state_bank,
            stream.state_transition,
            bootstrap.range_state_after_parameters);
        if (!status.ok()) {
            continue;
        }

        std::array<std::int64_t, kProbeSampleCount> diffs{};
        bool all_zero = true;
        for (auto& difference : diffs) {
            status = reader.read_signed(0, 0, difference);
            if (!status.ok()) {
                all_zero = false;
                break;
            }
            all_zero = all_zero && difference == 0;
        }
        if (!all_zero) {
            continue;
        }

        if (match_count < 8) {
            out << " state" << candidate << "=[";
            for (std::size_t i = 0; i < diffs.size(); ++i) {
                if (i != 0) {
                    out << ",";
                }
                out << diffs[i];
            }
            out << "]";
        }
        ++match_count;
    }
    out << " matches=" << match_count;
    append_best_matches(out, "custom_best", stream.state_transition, InitialStatePattern::Uniform);
    append_best_matches(out, "default_best", mffv1::syntax::kDefaultStateTransition, InitialStatePattern::Uniform);
    append_best_matches(out, "swapped_custom_best", swapped_custom_transition, InitialStatePattern::Uniform);
    append_best_matches(out, "zero_only_best", stream.state_transition, InitialStatePattern::ZeroOnly);
    append_best_matches(out, "frozen_zero_best", stream.state_transition, InitialStatePattern::FrozenZero);
    append_best_matches(out, "exponent_only_best", stream.state_transition, InitialStatePattern::ExponentOnly);
    append_best_matches(out, "sign_only_best", stream.state_transition, InitialStatePattern::SignOnly);
    append_best_matches(out, "magnitude_only_best", stream.state_transition, InitialStatePattern::MagnitudeOnly);
    append_zero_state_trace(out, 255);
    append_pivot_low_probe(out, 255);
    append_refill_variant_probe(out, 255);
    return out.str();
}

std::string describe_legacy_range_expected_residual_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    const auto& stream = bootstrap.stream;
    if (stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()
        || vector.expected_planes.empty()
        || vector.expected_planes.front().empty()
        || stream.quant_table_sets.size() != 1
        || stream.quant_table_sets.front().context_count == 0) {
        return {};
    }

    const auto& expected = vector.expected_planes.front().front();
    if (expected.info.sample_format != mffv1::SampleFormat::UInt8
        || expected.info.width == 0
        || expected.info.height == 0) {
        return {};
    }

    constexpr std::size_t kProbeSampleCount = 16;
    const std::array<std::size_t, 1> context_counts{
        stream.quant_table_sets.front().context_count,
    };
    mffv1::entropy::RangeCoder reader;
    auto status = reader.reset_from_arithmetic_state(
        vector.frame_payloads.front(),
        context_counts,
        {},
        stream.state_transition,
        bootstrap.range_state_after_parameters);
    if (!status.ok()) {
        return std::string{" expected_residual_probe="} + describe_status(status);
    }
    if (stream.version == 0) {
        status = reader.set_legacy_v0_arithmetic(true);
        if (!status.ok()) {
            return std::string{" expected_residual_probe="} + describe_status(status);
        }
    }

    const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
    mffv1::syntax::LineState line;
    status = line.reset(expected.info.width);
    if (!status.ok()) {
        return std::string{" expected_residual_probe="} + describe_status(status);
    }

    std::ostringstream out;
    out << " expected_residual_probe";
    const auto state_at = [](
                              const mffv1::entropy::RangeCoder::ContextStateBanks& contexts,
                              std::size_t context,
                              std::size_t state_index) -> int {
        if (contexts.empty()
            || context >= contexts.front().size()
            || state_index >= contexts.front()[context].size()) {
            return -1;
        }
        return static_cast<int>(contexts.front()[context][state_index]);
    };
    const auto append_scalar_state_summary = [&state_at](
                                                 std::ostringstream& trace,
                                                 const mffv1::entropy::RangeCoder::ContextStateBanks& before,
                                                 const mffv1::entropy::RangeCoder::ContextStateBanks& after,
                                                 std::size_t context) {
        trace << " s[0]=" << state_at(before, context, 0)
              << "->" << state_at(after, context, 0)
              << " s[1]=" << state_at(before, context, 1)
              << "->" << state_at(after, context, 1)
              << " s[11]=" << state_at(before, context, 11)
              << "->" << state_at(after, context, 11)
              << " s[22]=" << state_at(before, context, 22)
              << "->" << state_at(after, context, 22);
    };
    const auto append_first_sample_zero_state_probe = [&](
                                                          std::ostringstream& trace,
                                                          const mffv1::entropy::RangeCoder::ArithmeticState& state,
                                                          std::uint32_t prediction,
                                                          mffv1::entropy::ContextId context_id,
                                                          bool invert_difference,
                                                          std::uint32_t expected_sample) {
        if (stream.version != 0) {
            return;
        }
        constexpr std::array<std::uint8_t, 8> candidates{
            0, 1, 32, 64, 96, 128, 192, 255};
        struct CandidateResult {
            std::uint16_t state = 0;
            std::int64_t difference = 0;
            std::uint32_t sample = 0;
            std::uint32_t error = 0;
            bool valid = false;
        };
        const auto evaluate_candidate_at_state = [&](std::uint8_t candidate,
                                                     const mffv1::entropy::RangeCoder::ArithmeticState& arithmetic_state,
                                                     CandidateResult& result) {
            result.state = candidate;
            mffv1::entropy::RangeCoder::ScalarContextStates states{};
            states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
            states[0] = candidate;
            const std::array state_bank{
                std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{&states, 1},
            };
            mffv1::entropy::RangeCoder probe_reader;
            auto probe_status = probe_reader.reset_from_arithmetic_state(
                vector.frame_payloads.front(),
                context_counts,
                {},
                stream.state_transition,
                arithmetic_state);
            if (probe_status.ok()) {
                probe_status = probe_reader.set_legacy_v0_arithmetic(true);
            }
            if (probe_status.ok()) {
                probe_status = probe_reader.reset_from_arithmetic_state(
                    vector.frame_payloads.front(),
                    context_counts,
                    state_bank,
                    stream.state_transition,
                    arithmetic_state);
            }
            if (!probe_status.ok()) {
                return probe_status;
            }

            std::int64_t difference = 0;
            probe_status = probe_reader.read_signed(0, context_id, difference);
            if (!probe_status.ok()) {
                return probe_status;
            }
            auto reconstruction_difference = difference;
            if (invert_difference) {
                reconstruction_difference = -reconstruction_difference;
            }
            const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                prediction,
                static_cast<std::int32_t>(reconstruction_difference),
                stream.bits_per_raw_sample);
            result.difference = difference;
            result.sample = static_cast<std::uint32_t>(reconstructed);
            result.error = result.sample > expected_sample
                ? result.sample - expected_sample
                : expected_sample - result.sample;
            result.valid = true;
            return probe_status;
        };
        const auto evaluate_candidate = [&](std::uint8_t candidate,
                                            CandidateResult& result) {
            return evaluate_candidate_at_state(candidate, state, result);
        };
        const auto append_candidate = [&](std::uint8_t candidate) {
            CandidateResult result;
            const auto probe_status = evaluate_candidate(candidate, result);
            trace << " s0=" << static_cast<int>(candidate) << ":";
            if (!probe_status.ok()) {
                trace << "err(" << describe_status(probe_status) << ")";
                return;
            }
            trace << result.difference << "/" << result.sample
                  << (result.sample == expected_sample
                          ? ":ok"
                          : ":bad");
        };

        trace << " first_s0_probe";
        for (const auto candidate : candidates) {
            append_candidate(candidate);
        }

        std::array<CandidateResult, 8> best{};
        std::size_t best_count = 0;
        std::size_t exact_count = 0;
        for (std::uint16_t candidate = 0; candidate <= 255; ++candidate) {
            CandidateResult result;
            const auto probe_status = evaluate_candidate(
                static_cast<std::uint8_t>(candidate),
                result);
            if (!probe_status.ok() || !result.valid) {
                continue;
            }
            if (result.sample == expected_sample) {
                ++exact_count;
            }
            if (best_count < best.size()) {
                best[best_count] = result;
                ++best_count;
            } else if (result.error < best[best_count - 1].error) {
                best[best_count - 1] = result;
            } else {
                continue;
            }
            std::sort(
                best.begin(),
                best.begin() + static_cast<std::ptrdiff_t>(best_count),
                [](const CandidateResult& lhs, const CandidateResult& rhs) {
                    if (lhs.error != rhs.error) {
                        return lhs.error < rhs.error;
                    }
                    return lhs.state < rhs.state;
                });
        }
        trace << " exact=" << exact_count << " best";
        for (std::size_t i = 0; i < best_count; ++i) {
            trace << " s0=" << best[i].state
                  << ":" << best[i].difference
                  << "/" << best[i].sample
                  << "/err" << best[i].error;
        }

        std::array<CandidateResult, 8> low_best{};
        std::size_t low_best_count = 0;
        std::size_t low_exact_count = 0;
        for (std::uint32_t low = 0; low < state.range; ++low) {
            auto low_state = state;
            low_state.low = low;
            CandidateResult result;
            const auto probe_status = evaluate_candidate_at_state(255, low_state, result);
            if (!probe_status.ok() || !result.valid) {
                continue;
            }
            if (result.sample == expected_sample) {
                ++low_exact_count;
            }
            result.state = static_cast<std::uint16_t>(
                low > static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max())
                    ? std::numeric_limits<std::uint16_t>::max()
                    : low);
            if (low_best_count < low_best.size()) {
                low_best[low_best_count] = result;
                ++low_best_count;
            } else if (result.error < low_best[low_best_count - 1].error) {
                low_best[low_best_count - 1] = result;
            } else {
                continue;
            }
            std::sort(
                low_best.begin(),
                low_best.begin() + static_cast<std::ptrdiff_t>(low_best_count),
                [](const CandidateResult& lhs, const CandidateResult& rhs) {
                    if (lhs.error != rhs.error) {
                        return lhs.error < rhs.error;
                    }
                    return lhs.state < rhs.state;
                });
        }
        trace << " low_sweep_s0_255 exact=" << low_exact_count << " best";
        for (std::size_t i = 0; i < low_best_count; ++i) {
            trace << " low=" << low_best[i].state
                  << ":" << low_best[i].difference
                  << "/" << low_best[i].sample
                  << "/err" << low_best[i].error;
        }

        struct ByteLowResult {
            std::uint64_t byte_position = 0;
            std::uint32_t low = 0;
            std::int64_t difference = 0;
            std::uint32_t sample = 0;
            std::uint32_t error = 0;
        };
        std::array<ByteLowResult, 8> byte_low_best{};
        std::size_t byte_low_best_count = 0;
        std::size_t byte_low_exact_count = 0;
        const auto byte_begin = state.byte_position > 2 ? state.byte_position - 2 : std::uint64_t{0};
        const auto byte_end = std::min<std::uint64_t>(
            state.byte_position + 4,
            static_cast<std::uint64_t>(vector.frame_payloads.front().size()));
        for (auto byte_position = byte_begin; byte_position <= byte_end; ++byte_position) {
            for (std::uint32_t low = 0; low < state.range; ++low) {
                auto candidate_state = state;
                candidate_state.byte_position = byte_position;
                candidate_state.end = byte_position >= vector.frame_payloads.front().size();
                candidate_state.low = low;
                CandidateResult result;
                const auto probe_status = evaluate_candidate_at_state(
                    255,
                    candidate_state,
                    result);
                if (!probe_status.ok() || !result.valid) {
                    continue;
                }
                if (result.sample == expected_sample) {
                    ++byte_low_exact_count;
                }
                ByteLowResult byte_low_result{
                    byte_position,
                    low,
                    result.difference,
                    result.sample,
                    result.error,
                };
                if (byte_low_best_count < byte_low_best.size()) {
                    byte_low_best[byte_low_best_count] = byte_low_result;
                    ++byte_low_best_count;
                } else if (byte_low_result.error < byte_low_best[byte_low_best_count - 1].error) {
                    byte_low_best[byte_low_best_count - 1] = byte_low_result;
                } else {
                    continue;
                }
                std::sort(
                    byte_low_best.begin(),
                    byte_low_best.begin() + static_cast<std::ptrdiff_t>(byte_low_best_count),
                    [](const ByteLowResult& lhs, const ByteLowResult& rhs) {
                        if (lhs.error != rhs.error) {
                            return lhs.error < rhs.error;
                        }
                        if (lhs.byte_position != rhs.byte_position) {
                            return lhs.byte_position < rhs.byte_position;
                        }
                        return lhs.low < rhs.low;
                    });
            }
        }
        trace << " byte_low_sweep_s0_255 exact=" << byte_low_exact_count << " best";
        for (std::size_t i = 0; i < byte_low_best_count; ++i) {
            trace << " @" << byte_low_best[i].byte_position
                  << "/low=" << byte_low_best[i].low
                  << ":" << byte_low_best[i].difference
                  << "/" << byte_low_best[i].sample
                  << "/err" << byte_low_best[i].error;
        }

        struct BodyStateResult {
            std::uint16_t zero_state = 0;
            std::uint16_t body_state = 0;
            std::int64_t difference = 0;
            std::uint32_t sample = 0;
            std::uint32_t error = 0;
            std::uint32_t away_sample = 0;
            std::uint32_t away_error = 0;
        };
        const auto evaluate_body_state_pair = [&](std::uint8_t zero_state,
                                                  std::uint8_t body_state,
                                                  bool legacy_arithmetic,
                                                  BodyStateResult& result) {
            result.zero_state = zero_state;
            result.body_state = body_state;
            mffv1::entropy::RangeCoder::ScalarContextStates states{};
            states.fill(body_state);
            states[0] = zero_state;
            const std::array state_bank{
                std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{&states, 1},
            };
            mffv1::entropy::RangeCoder probe_reader;
            auto probe_status = probe_reader.reset_from_arithmetic_state(
                vector.frame_payloads.front(),
                context_counts,
                {},
                stream.state_transition,
                state);
            if (probe_status.ok() && legacy_arithmetic) {
                probe_status = probe_reader.set_legacy_v0_arithmetic(true);
            }
            if (probe_status.ok()) {
                probe_status = probe_reader.reset_from_arithmetic_state(
                    vector.frame_payloads.front(),
                    context_counts,
                    state_bank,
                    stream.state_transition,
                    state);
            }
            if (!probe_status.ok()) {
                return probe_status;
            }
            std::int64_t difference = 0;
            probe_status = probe_reader.read_signed(0, context_id, difference);
            if (!probe_status.ok()) {
                return probe_status;
            }
            auto reconstruction_difference = difference;
            if (invert_difference) {
                reconstruction_difference = -reconstruction_difference;
            }
            const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                prediction,
                static_cast<std::int32_t>(reconstruction_difference),
                stream.bits_per_raw_sample);
            result.difference = difference;
            result.sample = static_cast<std::uint32_t>(reconstructed);
            result.error = result.sample > expected_sample
                ? result.sample - expected_sample
                : expected_sample - result.sample;
            auto adjusted_difference = difference;
            if (adjusted_difference > 0) {
                ++adjusted_difference;
            } else if (adjusted_difference < 0) {
                --adjusted_difference;
            }
            if (invert_difference) {
                adjusted_difference = -adjusted_difference;
            }
            const auto away_reconstructed = mffv1::syntax::Predictor::reconstruct(
                prediction,
                static_cast<std::int32_t>(adjusted_difference),
                stream.bits_per_raw_sample);
            result.away_sample = static_cast<std::uint32_t>(away_reconstructed);
            result.away_error = result.away_sample > expected_sample
                ? result.away_sample - expected_sample
                : expected_sample - result.away_sample;
            return probe_status;
        };
        const auto append_body_state_sweep = [&](std::string_view label,
                                                 bool legacy_arithmetic) {
            std::array<BodyStateResult, 8> body_best{};
            std::size_t body_best_count = 0;
            std::size_t body_exact_count = 0;
            std::size_t away_exact_count = 0;
            std::array<BodyStateResult, 8> away_best{};
            std::size_t away_best_count = 0;
            const auto sort_body_results = [](auto begin, auto end, bool use_away_error) {
                std::sort(
                    begin,
                    end,
                    [use_away_error](const BodyStateResult& lhs, const BodyStateResult& rhs) {
                        const auto lhs_error = use_away_error ? lhs.away_error : lhs.error;
                        const auto rhs_error = use_away_error ? rhs.away_error : rhs.error;
                        if (lhs_error != rhs_error) {
                            return lhs_error < rhs_error;
                        }
                        if (lhs.zero_state != rhs.zero_state) {
                            return lhs.zero_state < rhs.zero_state;
                        }
                        return lhs.body_state < rhs.body_state;
                    });
            };
            const auto update_best = [&sort_body_results](
                                         std::array<BodyStateResult, 8>& best,
                                         std::size_t& best_count,
                                         const BodyStateResult& result,
                                         bool use_away_error) {
                if (best_count < best.size()) {
                    best[best_count] = result;
                    ++best_count;
                } else {
                    const auto candidate_error = use_away_error ? result.away_error : result.error;
                    const auto worst_error = use_away_error
                        ? best[best_count - 1].away_error
                        : best[best_count - 1].error;
                    if (candidate_error < worst_error) {
                        best[best_count - 1] = result;
                    } else {
                        return;
                    }
                }
                sort_body_results(
                    best.begin(),
                    best.begin() + static_cast<std::ptrdiff_t>(best_count),
                    use_away_error);
            };
            for (std::uint16_t zero_state = 0; zero_state <= 255; ++zero_state) {
                for (std::uint16_t body_state = 0; body_state <= 255; ++body_state) {
                    BodyStateResult result;
                    const auto probe_status = evaluate_body_state_pair(
                        static_cast<std::uint8_t>(zero_state),
                        static_cast<std::uint8_t>(body_state),
                        legacy_arithmetic,
                        result);
                    if (!probe_status.ok()) {
                        continue;
                    }
                    if (result.sample == expected_sample) {
                        ++body_exact_count;
                    }
                    if (result.away_sample == expected_sample) {
                        ++away_exact_count;
                    }
                    update_best(body_best, body_best_count, result, false);
                    update_best(away_best, away_best_count, result, true);
                }
            }
            trace << " " << label << " exact=" << body_exact_count << " best";
            for (std::size_t i = 0; i < body_best_count; ++i) {
                trace << " s0=" << body_best[i].zero_state
                      << "/body=" << body_best[i].body_state
                      << ":" << body_best[i].difference
                      << "/" << body_best[i].sample
                      << "/err" << body_best[i].error;
            }
            trace << " away_exact=" << away_exact_count << " away_best";
            for (std::size_t i = 0; i < away_best_count; ++i) {
                trace << " s0=" << away_best[i].zero_state
                      << "/body=" << away_best[i].body_state
                      << ":" << away_best[i].difference
                      << "/" << away_best[i].away_sample
                      << "/err" << away_best[i].away_error;
            }
        };
        append_body_state_sweep("s0_body_sweep", true);
        append_body_state_sweep("normal_s0_body_sweep", false);

        struct BodyFullResult {
            std::uint16_t zero_state = 0;
            std::uint16_t body_state = 0;
            std::size_t matched_samples = 0;
            std::uint32_t mismatch_x = 0;
            std::uint32_t mismatch_y = 0;
            std::int64_t difference = 0;
            std::uint32_t sample = 0;
            std::uint32_t expected = 0;
            bool failed = false;
        };
        const auto measure_body_full = [&](std::uint8_t zero_state,
                                           std::uint8_t body_state,
                                           bool legacy_arithmetic,
                                           bool away_from_zero,
                                           BodyFullResult& result) {
            result.zero_state = zero_state;
            result.body_state = body_state;
            mffv1::entropy::RangeCoder::ScalarContextStates states{};
            states.fill(body_state);
            states[0] = zero_state;
            const std::array state_bank{
                std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{&states, 1},
            };
            mffv1::entropy::RangeCoder probe_reader;
            auto probe_status = probe_reader.reset_from_arithmetic_state(
                vector.frame_payloads.front(),
                context_counts,
                {},
                stream.state_transition,
                state);
            if (probe_status.ok() && legacy_arithmetic) {
                probe_status = probe_reader.set_legacy_v0_arithmetic(true);
            }
            if (probe_status.ok()) {
                probe_status = probe_reader.reset_from_arithmetic_state(
                    vector.frame_payloads.front(),
                    context_counts,
                    state_bank,
                    stream.state_transition,
                    state);
            }
            if (!probe_status.ok()) {
                result.failed = true;
                return false;
            }

            const mffv1::syntax::ContextModel full_context_model(
                stream.quant_table_sets.front());
            mffv1::syntax::LineState full_line;
            probe_status = full_line.reset(expected.info.width);
            if (!probe_status.ok()) {
                result.failed = true;
                return false;
            }
            for (std::uint32_t yy = 0; yy < expected.info.height; ++yy) {
                for (std::uint32_t xx = 0; xx < expected.info.width; ++xx) {
                    const auto full_neighbors = full_line.neighbors(xx);
                    const auto full_prediction = mffv1::syntax::Predictor::median_predict(
                        full_neighbors.left,
                        full_neighbors.top,
                        full_neighbors.top_left);
                    mffv1::syntax::ContextDecision full_context;
                    probe_status = full_context_model.derive_context(
                        full_neighbors, full_context);
                    if (!probe_status.ok()) {
                        result.failed = true;
                        return false;
                    }
                    std::int64_t difference64 = 0;
                    probe_status = probe_reader.read_signed(
                        0, full_context.context, difference64);
                    if (!probe_status.ok()) {
                        result.failed = true;
                        return false;
                    }
                    if (away_from_zero) {
                        if (difference64 > 0) {
                            ++difference64;
                        } else if (difference64 < 0) {
                            --difference64;
                        }
                    }
                    if (full_context.invert_difference) {
                        difference64 = -difference64;
                    }
                    const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                        full_prediction,
                        static_cast<std::int32_t>(difference64),
                        stream.bits_per_raw_sample);
                    const auto full_expected_sample = sample_at(
                        expected.samples,
                        expected.info,
                        xx,
                        yy);
                    if (static_cast<std::uint32_t>(reconstructed) != full_expected_sample) {
                        result.mismatch_x = xx;
                        result.mismatch_y = yy;
                        result.difference = difference64;
                        result.sample = static_cast<std::uint32_t>(reconstructed);
                        result.expected = full_expected_sample;
                        return true;
                    }
                    full_line.mutable_current()[xx] = reconstructed;
                    ++result.matched_samples;
                }
                full_line.swap_lines();
            }
            return true;
        };
        const auto append_body_full_sweep = [&](std::string_view label,
                                                bool legacy_arithmetic,
                                                bool away_from_zero) {
            std::array<BodyFullResult, 8> best{};
            std::size_t best_count = 0;
            std::size_t exact_count = 0;
            const auto sort_results = [](auto begin, auto end) {
                std::sort(begin, end, [](const BodyFullResult& lhs,
                                         const BodyFullResult& rhs) {
                    if (lhs.matched_samples != rhs.matched_samples) {
                        return lhs.matched_samples > rhs.matched_samples;
                    }
                    if (lhs.zero_state != rhs.zero_state) {
                        return lhs.zero_state < rhs.zero_state;
                    }
                    return lhs.body_state < rhs.body_state;
                });
            };
            for (std::uint16_t zero_state = 0; zero_state <= 255; ++zero_state) {
                for (std::uint16_t body_state = 0; body_state <= 255; ++body_state) {
                    BodyFullResult result;
                    if (!measure_body_full(static_cast<std::uint8_t>(zero_state),
                                           static_cast<std::uint8_t>(body_state),
                                           legacy_arithmetic,
                                           away_from_zero,
                                           result)) {
                        continue;
                    }
                    if (result.matched_samples == expected.info.width * expected.info.height) {
                        ++exact_count;
                    }
                    if (best_count < best.size()) {
                        best[best_count] = result;
                        ++best_count;
                    } else if (result.matched_samples > best[best_count - 1].matched_samples) {
                        best[best_count - 1] = result;
                    } else {
                        continue;
                    }
                    sort_results(
                        best.begin(),
                        best.begin() + static_cast<std::ptrdiff_t>(best_count));
                }
            }
            trace << " " << label << " exact=" << exact_count << " best";
            for (std::size_t i = 0; i < best_count; ++i) {
                trace << " s0=" << best[i].zero_state
                      << "/body=" << best[i].body_state
                      << ":" << best[i].matched_samples
                      << "@" << best[i].mismatch_x << "," << best[i].mismatch_y
                      << "=" << best[i].sample << "/" << best[i].expected
                      << "/d" << best[i].difference;
            }
        };
        append_body_full_sweep("s0_body_full", true, false);
        append_body_full_sweep("s0_body_full_away", true, true);
    };
    const auto append_mode_probe = [&] {
        if (stream.version != 0) {
            return;
        }
        struct ModeCandidate {
            std::string_view label;
            bool legacy_arithmetic = false;
            std::uint8_t zero_state = mffv1::entropy::RangeCoder::kDefaultInitialState;
        };
        constexpr std::array candidates{
            ModeCandidate{"legacy_s255", true, 255},
            ModeCandidate{"legacy_s128", true, 128},
            ModeCandidate{"normal_s128", false, 128},
            ModeCandidate{"normal_s255", false, 255},
        };
        const auto measure = [&](const ModeCandidate& candidate) {
            struct Result {
                std::size_t matched = 0;
                std::uint32_t mismatch_x = 0;
                std::uint32_t mismatch_y = 0;
                std::uint32_t sample = 0;
                std::uint32_t expected_sample = 0;
                std::int64_t difference = 0;
                bool failed = false;
            } result;

            mffv1::entropy::RangeCoder::ScalarContextStates states{};
            states.fill(mffv1::entropy::RangeCoder::kDefaultInitialState);
            states[0] = candidate.zero_state;
            const std::array state_bank{
                std::span<const mffv1::entropy::RangeCoder::ScalarContextStates>{&states, 1},
            };
            mffv1::entropy::RangeCoder probe_reader;
            auto probe_status = probe_reader.reset_from_arithmetic_state(
                vector.frame_payloads.front(),
                context_counts,
                {},
                stream.state_transition,
                bootstrap.range_state_after_parameters);
            if (probe_status.ok() && candidate.legacy_arithmetic) {
                probe_status = probe_reader.set_legacy_v0_arithmetic(true);
            }
            if (probe_status.ok()) {
                probe_status = probe_reader.reset_from_arithmetic_state(
                    vector.frame_payloads.front(),
                    context_counts,
                    state_bank,
                    stream.state_transition,
                    bootstrap.range_state_after_parameters);
            }
            if (!probe_status.ok()) {
                result.failed = true;
                return result;
            }

            const mffv1::syntax::ContextModel probe_context_model(
                stream.quant_table_sets.front());
            mffv1::syntax::LineState probe_line;
            probe_status = probe_line.reset(expected.info.width);
            if (!probe_status.ok()) {
                result.failed = true;
                return result;
            }
            for (std::uint32_t yy = 0; yy < expected.info.height; ++yy) {
                for (std::uint32_t xx = 0; xx < expected.info.width; ++xx) {
                    const auto neighbors = probe_line.neighbors(xx);
                    const auto prediction = mffv1::syntax::Predictor::median_predict(
                        neighbors.left,
                        neighbors.top,
                        neighbors.top_left);
                    mffv1::syntax::ContextDecision context;
                    probe_status = probe_context_model.derive_context(neighbors, context);
                    if (!probe_status.ok()) {
                        result.failed = true;
                        return result;
                    }
                    std::int64_t difference64 = 0;
                    probe_status = probe_reader.read_signed(
                        0, context.context, difference64);
                    if (!probe_status.ok()) {
                        result.failed = true;
                        return result;
                    }
                    if (context.invert_difference) {
                        difference64 = -difference64;
                    }
                    const auto reconstructed = mffv1::syntax::Predictor::reconstruct(
                        prediction,
                        static_cast<std::int32_t>(difference64),
                        stream.bits_per_raw_sample);
                    const auto expected_sample = sample_at(
                        expected.samples,
                        expected.info,
                        xx,
                        yy);
                    if (static_cast<std::uint32_t>(reconstructed) != expected_sample) {
                        result.mismatch_x = xx;
                        result.mismatch_y = yy;
                        result.sample = static_cast<std::uint32_t>(reconstructed);
                        result.expected_sample = expected_sample;
                        result.difference = difference64;
                        return result;
                    }
                    probe_line.mutable_current()[xx] = reconstructed;
                    ++result.matched;
                }
                probe_line.swap_lines();
            }
            return result;
        };

        out << " mode_probe";
        for (const auto& candidate : candidates) {
            const auto result = measure(candidate);
            out << " " << candidate.label << "=" << result.matched;
            if (result.failed) {
                out << "/fail";
            } else if (result.matched != expected.info.width * expected.info.height) {
                out << "@" << result.mismatch_x << "," << result.mismatch_y
                    << ":" << result.sample << "/" << result.expected_sample
                    << "/d" << result.difference;
            }
        }
    };
    append_mode_probe();
    std::size_t decoded_samples = 0;
    std::ostringstream compact_sequence;
    compact_sequence << " compact_seq";
    const auto append_compact_state = [](
                                          std::ostringstream& trace,
                                          const mffv1::entropy::RangeCoder::ArithmeticState& arithmetic_state) {
        trace << " r=" << std::hex << arithmetic_state.range
              << "/l=" << arithmetic_state.low
              << std::dec
              << "/b=" << arithmetic_state.byte_position;
    };
    for (std::uint32_t y = 0; y < expected.info.height; ++y) {
        for (std::uint32_t x = 0; x < expected.info.width; ++x) {
            if (decoded_samples >= kProbeSampleCount) {
                out << compact_sequence.str();
                return out.str();
            }
            const auto neighbors = line.neighbors(x);
            const auto prediction = mffv1::syntax::Predictor::median_predict(
                neighbors.left,
                neighbors.top,
                neighbors.top_left);
            mffv1::syntax::ContextDecision context;
            status = context_model.derive_context(neighbors, context);
            if (!status.ok()) {
                out << " err(" << describe_status(status) << ")";
                return out.str();
            }
            const auto expected_sample = sample_at(expected.samples, expected.info, x, y);
            auto expected_difference = mffv1::syntax::Predictor::difference(
                static_cast<std::int32_t>(expected_sample),
                prediction,
                stream.bits_per_raw_sample);
            if (context.invert_difference) {
                expected_difference = -expected_difference;
            }

            const auto before_state = reader.arithmetic_state();
            mffv1::entropy::RangeCoder::ContextStateBanks before_contexts;
            status = reader.copy_contexts(before_contexts);
            if (!status.ok()) {
                out << " #" << decoded_samples
                    << "@(" << x << "," << y << ")=context_err("
                    << describe_status(status) << ")";
                return out.str();
            }
            std::int64_t actual_difference = 0;
            status = reader.read_signed(0, context.context, actual_difference);
            if (!status.ok()) {
                out << " #" << decoded_samples
                    << "@(" << x << "," << y << ")=err("
                    << describe_status(status) << ")";
                return out.str();
            }
            const auto after_state = reader.arithmetic_state();
            mffv1::entropy::RangeCoder::ContextStateBanks after_contexts;
            status = reader.copy_contexts(after_contexts);
            if (!status.ok()) {
                out << " #" << decoded_samples
                    << "@(" << x << "," << y << ")=context_err("
                    << describe_status(status) << ")";
                return out.str();
            }
            auto reconstruction_difference = actual_difference;
            if (context.invert_difference) {
                reconstruction_difference = -reconstruction_difference;
            }
            const auto actual_sample = mffv1::syntax::Predictor::reconstruct(
                prediction,
                static_cast<std::int32_t>(reconstruction_difference),
                stream.bits_per_raw_sample);

            compact_sequence << " #" << decoded_samples
                             << ":c" << context.context
                             << (context.invert_difference ? "i" : "")
                             << "/p" << prediction
                             << "/e" << expected_difference
                             << "/a" << actual_difference
                             << "/s" << state_at(before_contexts, context.context, 0)
                             << "->" << state_at(after_contexts, context.context, 0);
            append_compact_state(compact_sequence, before_state);
            compact_sequence << "=>" << static_cast<int>(actual_sample)
                             << "/" << expected_sample;

            out << " #" << decoded_samples
                << "@(" << x << "," << y << ")"
                << " ctx=" << context.context
                << " inv=" << context.invert_difference
                << " pred=" << prediction
                << " exp_sample=" << expected_sample
                << " exp_diff=" << expected_difference
                << " act_diff=" << actual_difference
                << " act_sample=" << static_cast<int>(actual_sample);
            if (expected_sample != 0 || actual_sample != 0) {
                out << " arith{"
                    << describe_range_state(before_state)
                    << "->"
                    << describe_range_state(after_state)
                    << "}";
                append_scalar_state_summary(
                    out,
                    before_contexts,
                    after_contexts,
                    context.context);
                if (decoded_samples == 0) {
                    append_first_sample_zero_state_probe(
                        out,
                        before_state,
                        prediction,
                        context.context,
                        context.invert_difference,
                        expected_sample);
                }
            }
            line.mutable_current()[x] = actual_sample;
            ++decoded_samples;
        }
        line.swap_lines();
    }
    out << compact_sequence.str();
    return out.str();
}

std::string describe_legacy_range_slice_header_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    auto stream = bootstrap.stream;
    if (stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()) {
        return {};
    }
    stream.width = vector.frame_width;
    stream.height = vector.frame_height;

    const std::array<std::size_t, 1> context_counts{1};
    mffv1::entropy::RangeCoder reader;
    auto status = reader.reset_from_arithmetic_state(
        vector.frame_payloads.front(),
        context_counts,
        {},
        stream.state_transition,
        bootstrap.range_state_after_parameters);
    if (!status.ok()) {
        return std::string{" slice_header_probe="} + describe_status(status);
    }
    if (stream.version == 0) {
        status = reader.set_legacy_v0_arithmetic(true);
        if (!status.ok()) {
            return std::string{" slice_header_probe="} + describe_status(status);
        }
    }

    mffv1::syntax::SliceDescriptor descriptor;
    const mffv1::codec::SliceHeaderParser header_parser;
    status = header_parser.read_descriptor(reader, stream, descriptor);
    std::ostringstream out;
    out << " slice_header_probe";
    if (!status.ok()) {
        out << "=" << describe_status(status)
            << " after{"
            << describe_range_state(reader.arithmetic_state())
            << "}";
        return out.str();
    }

    out << " x=" << descriptor.x
        << " y=" << descriptor.y
        << " w=" << descriptor.width
        << " h=" << descriptor.height
        << " raster=" << descriptor.raster_x << "," << descriptor.raster_y
        << "+" << descriptor.raster_width << "x" << descriptor.raster_height
        << " content=" << descriptor.content_byte_offset
        << " qidx=";
    for (const auto index : descriptor.quant_table_set_indexes) {
        out << index << ",";
    }
    out << " after{"
        << describe_range_state(reader.arithmetic_state())
        << "}";
    if (!vector.expected_planes.empty()
        && !vector.expected_planes.front().empty()
        && !stream.quant_table_sets.empty()) {
        const auto& expected = vector.expected_planes.front().front();
        if (expected.info.width > 0
            && expected.info.height > 0
            && expected.info.sample_format == mffv1::SampleFormat::UInt8) {
            std::vector<std::size_t> sample_context_counts;
            sample_context_counts.reserve(stream.quant_table_sets.size());
            for (const auto& qset : stream.quant_table_sets) {
                sample_context_counts.push_back(qset.context_count);
            }
            status = reader.reconfigure_contexts(sample_context_counts);
            if (status.ok() && stream.version == 0) {
                status = reader.set_legacy_v0_arithmetic(true);
            }
            if (!status.ok()) {
                out << " first_after_header=err(" << describe_status(status) << ")";
                return out.str();
            }

            const auto before_sample = reader.arithmetic_state();
            const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
            mffv1::syntax::LineState line;
            status = line.reset(expected.info.width);
            if (!status.ok()) {
                out << " first_after_header=err(" << describe_status(status) << ")";
                return out.str();
            }
            const auto neighbors = line.neighbors(0);
            const auto prediction = mffv1::syntax::Predictor::median_predict(
                neighbors.left,
                neighbors.top,
                neighbors.top_left);
            mffv1::syntax::ContextDecision context;
            status = context_model.derive_context(neighbors, context);
            if (!status.ok()) {
                out << " first_after_header=err(" << describe_status(status) << ")";
                return out.str();
            }
            const auto expected_sample = sample_at(expected.samples, expected.info, 0, 0);
            auto expected_difference = mffv1::syntax::Predictor::difference(
                static_cast<std::int32_t>(expected_sample),
                prediction,
                stream.bits_per_raw_sample);
            if (context.invert_difference) {
                expected_difference = -expected_difference;
            }
            std::int64_t actual_difference = 0;
            status = reader.read_signed(0, context.context, actual_difference);
            if (!status.ok()) {
                out << " first_after_header=err(" << describe_status(status) << ")";
                return out.str();
            }
            auto reconstruction_difference = actual_difference;
            if (context.invert_difference) {
                reconstruction_difference = -reconstruction_difference;
            }
            const auto actual_sample = mffv1::syntax::Predictor::reconstruct(
                prediction,
                static_cast<std::int32_t>(reconstruction_difference),
                stream.bits_per_raw_sample);
            out << " first_after_header"
                << " ctx=" << context.context
                << " pred=" << prediction
                << " exp_sample=" << expected_sample
                << " exp_diff=" << expected_difference
                << " act_diff=" << actual_difference
                << " act_sample=" << static_cast<int>(actual_sample)
                << " before{"
                << describe_range_state(before_sample)
                << "} after{"
                << describe_range_state(reader.arithmetic_state())
                << "}";
        }
    }
    return out.str();
}

std::string describe_legacy_range_precontent_symbol_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    const auto& stream = bootstrap.stream;
    if (stream.version != 0
        || stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()
        || vector.expected_planes.empty()
        || vector.expected_planes.front().empty()
        || stream.quant_table_sets.empty()) {
        return {};
    }

    const auto& expected = vector.expected_planes.front().front();
    if (expected.info.width == 0
        || expected.info.height == 0
        || expected.info.sample_format != mffv1::SampleFormat::UInt8) {
        return {};
    }

    const std::array<std::size_t, 1> context_counts{
        stream.quant_table_sets.front().context_count,
    };
    const auto read_first_sample = [&](
                                       mffv1::entropy::RangeCoder& reader,
                                       std::ostringstream& out) {
        auto status = reader.reconfigure_contexts(context_counts);
        if (status.ok()) {
            status = reader.set_legacy_v0_arithmetic(true);
        }
        if (!status.ok()) {
            out << " sample=err(" << describe_status(status) << ")";
            return;
        }

        const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
        mffv1::syntax::LineState line;
        status = line.reset(expected.info.width);
        if (!status.ok()) {
            out << " sample=err(" << describe_status(status) << ")";
            return;
        }
        const auto neighbors = line.neighbors(0);
        const auto prediction = mffv1::syntax::Predictor::median_predict(
            neighbors.left,
            neighbors.top,
            neighbors.top_left);
        mffv1::syntax::ContextDecision context;
        status = context_model.derive_context(neighbors, context);
        if (!status.ok()) {
            out << " sample=err(" << describe_status(status) << ")";
            return;
        }
        std::int64_t difference = 0;
        const auto before = reader.arithmetic_state();
        status = reader.read_signed(0, context.context, difference);
        if (!status.ok()) {
            out << " sample=err(" << describe_status(status) << ")";
            return;
        }
        auto reconstruction_difference = difference;
        if (context.invert_difference) {
            reconstruction_difference = -reconstruction_difference;
        }
        const auto sample = mffv1::syntax::Predictor::reconstruct(
            prediction,
            static_cast<std::int32_t>(reconstruction_difference),
            stream.bits_per_raw_sample);
        const auto expected_sample = sample_at(expected.samples, expected.info, 0, 0);
        out << " sample=" << difference
            << "/" << static_cast<int>(sample)
            << "/" << expected_sample
            << " before{" << describe_range_state(before)
            << "} after{" << describe_range_state(reader.arithmetic_state()) << "}";
    };
    struct SkipSweepResult {
        std::size_t skip_count = 0;
        std::uint32_t sample = 0;
        std::uint32_t expected_sample = 0;
        std::uint32_t error = 0;
        std::int64_t difference = 0;
        mffv1::entropy::RangeCoder::ArithmeticState before_sample;
        bool failed = false;
    };
    const auto measure_after_unsigned_skips = [&](std::size_t skip_count) {
        SkipSweepResult result;
        result.skip_count = skip_count;
        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset_from_arithmetic_state(
            vector.frame_payloads.front(),
            context_counts,
            {},
            stream.state_transition,
            bootstrap.range_state_after_parameters);
        if (status.ok()) {
            status = reader.set_legacy_v0_arithmetic(true);
        }
        for (std::size_t i = 0; status.ok() && i < skip_count; ++i) {
            std::uint64_t skipped = 0;
            status = reader.read_unsigned(0, 0, skipped);
        }
        if (status.ok()) {
            status = reader.reconfigure_contexts(context_counts);
        }
        if (status.ok()) {
            status = reader.set_legacy_v0_arithmetic(true);
        }
        if (!status.ok()) {
            result.failed = true;
            return result;
        }

        const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
        mffv1::syntax::LineState line;
        status = line.reset(expected.info.width);
        if (!status.ok()) {
            result.failed = true;
            return result;
        }
        const auto neighbors = line.neighbors(0);
        const auto prediction = mffv1::syntax::Predictor::median_predict(
            neighbors.left,
            neighbors.top,
            neighbors.top_left);
        mffv1::syntax::ContextDecision context;
        status = context_model.derive_context(neighbors, context);
        if (!status.ok()) {
            result.failed = true;
            return result;
        }
        result.before_sample = reader.arithmetic_state();
        status = reader.read_signed(0, context.context, result.difference);
        if (!status.ok()) {
            result.failed = true;
            return result;
        }
        auto reconstruction_difference = result.difference;
        if (context.invert_difference) {
            reconstruction_difference = -reconstruction_difference;
        }
        const auto sample = mffv1::syntax::Predictor::reconstruct(
            prediction,
            static_cast<std::int32_t>(reconstruction_difference),
            stream.bits_per_raw_sample);
        result.sample = static_cast<std::uint32_t>(sample);
        result.expected_sample = sample_at(expected.samples, expected.info, 0, 0);
        result.error = result.sample > result.expected_sample
            ? result.sample - result.expected_sample
            : result.expected_sample - result.sample;
        return result;
    };

    std::ostringstream out;
    out << " precontent_symbol_probe";
    for (const bool signed_symbol : {false, true}) {
        for (std::size_t skip_count = 1; skip_count <= 4; ++skip_count) {
            mffv1::entropy::RangeCoder reader;
            auto status = reader.reset_from_arithmetic_state(
                vector.frame_payloads.front(),
                context_counts,
                {},
                stream.state_transition,
                bootstrap.range_state_after_parameters);
            if (status.ok()) {
                status = reader.set_legacy_v0_arithmetic(true);
            }
            out << " " << (signed_symbol ? "s" : "u") << skip_count << "=";
            if (!status.ok()) {
                out << "err(" << describe_status(status) << ")";
                continue;
            }
            bool failed = false;
            out << "[";
            for (std::size_t i = 0; i < skip_count; ++i) {
                if (i != 0) {
                    out << ",";
                }
                if (signed_symbol) {
                    std::int64_t value = 0;
                    status = reader.read_signed(0, 0, value);
                    out << value;
                } else {
                    std::uint64_t value = 0;
                    status = reader.read_unsigned(0, 0, value);
                    out << value;
                }
                if (!status.ok()) {
                    out << "err(" << describe_status(status) << ")";
                    failed = true;
                    break;
                }
            }
            out << "]";
            if (failed) {
                continue;
            }
            out << " after_skip{" << describe_range_state(reader.arithmetic_state()) << "}";
            read_first_sample(reader, out);
        }
    }
    std::array<SkipSweepResult, 6> best{};
    std::size_t best_count = 0;
    std::vector<std::size_t> exact_skips;
    const auto sort_best = [&] {
        std::sort(
            best.begin(),
            best.begin() + static_cast<std::ptrdiff_t>(best_count),
            [](const SkipSweepResult& lhs, const SkipSweepResult& rhs) {
                if (lhs.error != rhs.error) {
                    return lhs.error < rhs.error;
                }
                return lhs.skip_count < rhs.skip_count;
            });
    };
    for (std::size_t skip_count = 0; skip_count <= 512; ++skip_count) {
        const auto result = measure_after_unsigned_skips(skip_count);
        if (result.failed) {
            continue;
        }
        if (result.sample == result.expected_sample) {
            exact_skips.push_back(skip_count);
        }
        if (best_count < best.size()) {
            best[best_count] = result;
            ++best_count;
            sort_best();
        } else if (result.error < best[best_count - 1].error) {
            best[best_count - 1] = result;
            sort_best();
        }
    }
    out << " unsigned_skip_sweep exact=";
    if (exact_skips.empty()) {
        out << "none";
    } else {
        for (std::size_t i = 0; i < std::min<std::size_t>(exact_skips.size(), 8); ++i) {
            if (i != 0) {
                out << ",";
            }
            out << exact_skips[i];
        }
    }
    out << " best";
    for (std::size_t i = 0; i < best_count; ++i) {
        out << " n" << best[i].skip_count
            << ":" << best[i].difference
            << "/" << best[i].sample
            << "/err" << best[i].error
            << "@b" << best[i].before_sample.byte_position;
    }
    return out.str();
}

std::string describe_legacy_range_parameter_entry_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    const auto& stream = bootstrap.stream;
    if (stream.version != 0
        || stream.entropy_mode != mffv1::EntropyMode::Range
        || vector.frame_payloads.empty()
        || vector.expected_planes.empty()
        || vector.expected_planes.front().empty()
        || stream.quant_table_sets.empty()) {
        return {};
    }

    const auto& expected = vector.expected_planes.front().front();
    if (expected.info.width == 0
        || expected.info.height == 0
        || expected.info.sample_format != mffv1::SampleFormat::UInt8) {
        return {};
    }

    const std::array<std::size_t, 1> context_counts{
        stream.quant_table_sets.front().context_count,
    };
    struct EntryCandidate {
        std::string_view label;
        mffv1::entropy::RangeCoder::ArithmeticState arithmetic_state;
        bool use_custom_transition = false;
        bool legacy_arithmetic = true;
    };
    const std::array candidates{
        EntryCandidate{
            "after_key_default",
            bootstrap.range_state_after_keyframe,
            false,
            true,
        },
        EntryCandidate{
            "after_key_custom",
            bootstrap.range_state_after_keyframe,
            true,
            true,
        },
        EntryCandidate{
            "after_params_current",
            bootstrap.range_state_after_parameters,
            true,
            true,
        },
        EntryCandidate{
            "after_params_normal",
            bootstrap.range_state_after_parameters,
            true,
            false,
        },
    };

    const auto measure = [&](const EntryCandidate& candidate) {
        struct Result {
            std::int64_t difference = 0;
            std::uint32_t sample = 0;
            std::uint32_t expected_sample = 0;
            mffv1::entropy::RangeCoder::ArithmeticState before;
            mffv1::entropy::RangeCoder::ArithmeticState after;
            bool failed = false;
        } result;

        mffv1::entropy::RangeCoder reader;
        auto status = reader.reset_from_arithmetic_state(
            vector.frame_payloads.front(),
            context_counts,
            {},
            candidate.use_custom_transition
                ? stream.state_transition
                : mffv1::syntax::kDefaultStateTransition,
            candidate.arithmetic_state);
        if (status.ok() && candidate.legacy_arithmetic) {
            status = reader.set_legacy_v0_arithmetic(true);
        }
        if (!status.ok()) {
            result.failed = true;
            return result;
        }

        const mffv1::syntax::ContextModel context_model(stream.quant_table_sets.front());
        mffv1::syntax::LineState line;
        status = line.reset(expected.info.width);
        if (!status.ok()) {
            result.failed = true;
            return result;
        }
        const auto neighbors = line.neighbors(0);
        const auto prediction = mffv1::syntax::Predictor::median_predict(
            neighbors.left,
            neighbors.top,
            neighbors.top_left);
        mffv1::syntax::ContextDecision context;
        status = context_model.derive_context(neighbors, context);
        if (!status.ok()) {
            result.failed = true;
            return result;
        }
        result.before = reader.arithmetic_state();
        status = reader.read_signed(0, context.context, result.difference);
        if (!status.ok()) {
            result.failed = true;
            return result;
        }
        auto reconstruction_difference = result.difference;
        if (context.invert_difference) {
            reconstruction_difference = -reconstruction_difference;
        }
        const auto sample = mffv1::syntax::Predictor::reconstruct(
            prediction,
            static_cast<std::int32_t>(reconstruction_difference),
            stream.bits_per_raw_sample);
        result.sample = static_cast<std::uint32_t>(sample);
        result.expected_sample = sample_at(expected.samples, expected.info, 0, 0);
        result.after = reader.arithmetic_state();
        return result;
    };

    std::ostringstream out;
    out << " parameter_entry_probe";
    for (const auto& candidate : candidates) {
        const auto result = measure(candidate);
        out << " " << candidate.label << "=";
        if (result.failed) {
            out << "fail";
            continue;
        }
        out << result.difference
            << "/" << result.sample
            << "/" << result.expected_sample
            << " before{" << describe_range_state(result.before)
            << "} after{" << describe_range_state(result.after) << "}";
    }
    return out.str();
}

std::string legacy_v1_sibling_name(std::string_view name)
{
    constexpr std::string_view marker = "_v0_legacy_";
    const auto marker_pos = name.find(marker);
    if (marker_pos == std::string_view::npos) {
        return {};
    }

    std::string sibling{name};
    sibling.replace(marker_pos, marker.size(), "_v1_legacy_");
    return sibling;
}

std::string describe_legacy_range_v1_sibling_probe(std::string_view name)
{
    const auto sibling_name = legacy_v1_sibling_name(name);
    if (sibling_name.empty()
        || name.find("range_") == std::string_view::npos) {
        return {};
    }
    for (const auto& sibling : mffv1_testvectors::decode_vectors()) {
        if (sibling.name != sibling_name) {
            continue;
        }
        if (sibling.frame_payloads.empty()) {
            return " v1_sibling_probe=empty";
        }

        mffv1::codec::LegacyFrameBootstrap bootstrap;
        const mffv1::codec::LegacyFrameBootstrapParser bootstrap_parser;
        const auto status = bootstrap_parser.parse(
            sibling.frame_payloads.front(),
            sibling.frame_width,
            sibling.frame_height,
            bootstrap);
        if (!status.ok()) {
            return std::string{" v1_sibling_probe="} + describe_status(status);
        }
        return std::string{" v1_sibling{"}
            + sibling_name
            + " content=" + std::to_string(bootstrap.content_byte_offset)
            + " " + describe_stream_summary(bootstrap.stream)
            + " after_parameters{"
            + describe_range_state(bootstrap.range_state_after_parameters)
            + "}"
            + describe_payload_bytes(
                sibling.frame_payloads.front(),
                bootstrap.content_byte_offset,
                4,
                24,
                "content_bytes")
            + describe_legacy_range_parameter_trace(
                sibling, bootstrap.stream, "param_trace")
            + describe_legacy_range_expected_residual_probe(sibling, bootstrap)
            + describe_legacy_range_symbol_probe(
                sibling, bootstrap.stream, true, "range_probe")
            + "}";
    }
    return " v1_sibling_probe=missing";
}

std::string describe_legacy_bootstrap_state(
    const mffv1_testvectors::DecodeVector& vector)
{
    if (!vector.configuration_record.empty() || vector.frame_payloads.empty()) {
        return {};
    }

    mffv1::codec::LegacyFrameBootstrap bootstrap;
    const mffv1::codec::LegacyFrameBootstrapParser bootstrap_parser;
    const auto status = bootstrap_parser.parse(
        vector.frame_payloads.front(),
        vector.frame_width,
        vector.frame_height,
        bootstrap);
    if (!status.ok()) {
        return std::string{" bootstrap="} + describe_status(status);
    }

    std::ostringstream out;
    out << " bootstrap keyframe="
        << bootstrap.keyframe
        << " embedded="
        << bootstrap.has_embedded_parameters
        << " content="
        << bootstrap.content_byte_offset
        << " after_keyframe{"
        << describe_range_state(bootstrap.range_state_after_keyframe)
        << "}";
    if (bootstrap.has_embedded_parameters) {
        out << " "
            << describe_stream_summary(bootstrap.stream)
            << " after_parameters{"
            << describe_range_state(bootstrap.range_state_after_parameters)
            << "}"
            << describe_payload_bytes(
                vector.frame_payloads.front(),
                bootstrap.content_byte_offset,
                4,
                24,
                "content_bytes")
            << describe_legacy_range_parameter_trace(
                vector, bootstrap.stream, "param_trace")
            << describe_legacy_range_symbol_probe(
                vector, bootstrap.stream, true, "range_probe")
            << describe_legacy_range_symbol_probe(
                vector, bootstrap.stream, false, "range_probe_carry_context")
            << describe_legacy_range_expected_residual_probe(vector, bootstrap)
            << describe_legacy_range_slice_header_probe(vector, bootstrap)
            << describe_legacy_range_precontent_symbol_probe(vector, bootstrap)
            << describe_legacy_range_parameter_entry_probe(vector, bootstrap)
            << describe_legacy_golomb_rice_boundary_probe(vector, bootstrap)
            << describe_legacy_range_shifted_state_probe(vector, bootstrap)
            << describe_legacy_range_initial_state_probe(vector, bootstrap)
            << describe_legacy_range_reset_boundary_probe(vector, bootstrap)
            << describe_legacy_range_v1_sibling_probe(vector.name);
    }
    return out.str();
}

std::string describe_frame_parse(
    const mffv1_testvectors::DecodeVector& vector,
    std::span<const std::byte> frame_payload)
{
    mffv1::syntax::StreamParameters stream;
    mffv1::Status status;
    if (vector.configuration_record.empty()) {
        mffv1::codec::LegacyFrameBootstrap bootstrap;
        const mffv1::codec::LegacyFrameBootstrapParser bootstrap_parser;
        status = bootstrap_parser.parse(
            frame_payload, vector.frame_width, vector.frame_height, bootstrap);
        if (!status.ok()) {
            return std::string{"legacy bootstrap parse: "} + describe_status(status);
        }
        if (!bootstrap.has_embedded_parameters) {
            return "legacy bootstrap parse: no embedded parameters";
        }
        stream = std::move(bootstrap.stream);
    } else {
        mffv1::codec::ConfigurationRecordParser config_parser;
        status = config_parser.parse(vector.configuration_record, stream);
        if (!status.ok()) {
            return std::string{"config parse: "} + describe_status(status);
        }
        stream.width = vector.frame_width;
        stream.height = vector.frame_height;
    }

    mffv1::codec::FrameParser frame_parser(stream, true);
    mffv1::codec::FrameDecodeContext frame;
    status = frame_parser.parse(frame_payload, frame);
    if (!status.ok()) {
        return std::string{"frame parse: "} + describe_status(status);
    }

    std::ostringstream out;
    out << describe_stream_summary(stream);
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
    std::uint8_t bits_per_raw_sample,
    bool skip_unwritten = true)
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
        while (skip_unwritten
               && actual_it != actual_row.end()
               && *actual_it == std::byte{0xa5}) {
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

struct GolombRiceBoundaryCandidateSummary {
    std::uint64_t byte_offset = 0;
    std::uint8_t bit_offset = 0;
    std::size_t matched_samples = 0;
    mffv1::Status status;
    bool output_matches = false;
    std::string first_output_mismatch;
    bool measured = false;
};

GolombRiceBoundaryCandidateSummary measure_golomb_rice_boundary_candidate(
    const mffv1::syntax::StreamParameters& stream,
    const mffv1::syntax::SliceDescriptor& candidate,
    std::span<const mffv1_testvectors::PlaneVector> expected_planes)
{
    GolombRiceBoundaryCandidateSummary summary;
    summary.byte_offset = candidate.content_byte_offset;
    summary.bit_offset = candidate.content_bit_offset;

    std::vector<std::vector<std::byte>> plane_storage;
    std::vector<mffv1::MutablePlaneView> output_planes;
    plane_storage.reserve(expected_planes.size());
    output_planes.reserve(expected_planes.size());
    for (const auto& expected : expected_planes) {
        std::size_t expected_size = 0;
        if (!compute_plane_size(expected, expected_size)) {
            return summary;
        }
        plane_storage.emplace_back(expected_size, std::byte{0xa5});
        output_planes.push_back(
            mffv1::MutablePlaneView{plane_storage.back().data(), expected.info});
    }

    mffv1::MutableFrameView output{output_planes.data(), output_planes.size()};
    mffv1::codec::SliceOutputWindow window;
    auto status = window.validate(stream, output, candidate);
    if (!status.ok()) {
        summary.status = status;
        summary.measured = true;
        return summary;
    }
    mffv1::codec::SliceState state;
    status = state.reset(stream, window);
    if (!status.ok()) {
        summary.status = status;
        summary.measured = true;
        return summary;
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
            return summary;
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
    summary.status = status;
    summary.matched_samples = observer.matched_sample_count();
    summary.output_matches = true;
    for (std::size_t index = 0; index < expected_planes.size(); ++index) {
        std::size_t expected_size = 0;
        if (!compute_plane_size(expected_planes[index], expected_size)) {
            summary.output_matches = false;
            break;
        }
        const std::span<const std::byte> expected_bytes{
            expected_planes[index].samples.data(), expected_size};
        if (!std::equal(expected_bytes.begin(),
                        expected_bytes.end(),
                        plane_storage[index].begin(),
                        plane_storage[index].begin() + expected_size)) {
            summary.output_matches = false;
            if (summary.first_output_mismatch.empty()) {
                summary.first_output_mismatch = describe_first_partial_mismatch(
                    plane_storage[index],
                    expected_bytes,
                    index,
                    expected_planes[index].info,
                    stream.bits_per_raw_sample,
                    false);
            }
            break;
        }
    }
    summary.measured = true;
    return summary;
}

std::string describe_legacy_golomb_rice_boundary_probe(
    const mffv1_testvectors::DecodeVector& vector,
    const mffv1::codec::LegacyFrameBootstrap& bootstrap)
{
    auto stream = bootstrap.stream;
    if (stream.entropy_mode != mffv1::EntropyMode::GolombRice
        || vector.frame_payloads.empty()
        || vector.expected_planes.empty()
        || vector.expected_planes.front().empty()) {
        return {};
    }

    stream.width = vector.frame_width;
    stream.height = vector.frame_height;

    const auto payload = vector.frame_payloads.front();
    mffv1::syntax::SliceDescriptor candidate;
    candidate.index = 0;
    candidate.x = 0;
    candidate.y = 0;
    candidate.width = stream.width;
    candidate.height = stream.height;
    candidate.raster_x = 0;
    candidate.raster_y = 0;
    candidate.raster_width = 1;
    candidate.raster_height = 1;
    candidate.payload = payload;
    candidate.payload_byte_offset = 0;
    candidate.quant_table_set_indexes.push_back(0);

    const auto center = std::min<std::uint64_t>(
        bootstrap.content_byte_offset,
        static_cast<std::uint64_t>(payload.size()));
    const auto begin = stream.version == 0
        ? std::uint64_t{0}
        : (center > 2 ? center - 2 : std::uint64_t{0});
    const auto end = std::min<std::uint64_t>(
        center + 2,
        static_cast<std::uint64_t>(payload.size()));

    std::ostringstream out;
    out << " gr_boundary_probe";
    GolombRiceBoundaryCandidateSummary best_candidate;
    std::vector<GolombRiceBoundaryCandidateSummary> measured_candidates;
    for (auto offset = begin; offset <= end; ++offset) {
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            candidate.content_byte_offset = offset;
            candidate.content_bit_offset = bit;
            const auto summary = measure_golomb_rice_boundary_candidate(
                stream,
                candidate,
                vector.expected_planes.front());
            if (summary.measured) {
                measured_candidates.push_back(summary);
            }
            if (summary.measured
                && (!best_candidate.measured
                    || summary.matched_samples > best_candidate.matched_samples
                    || (summary.matched_samples == best_candidate.matched_samples
                        && summary.status.ok()
                        && !best_candidate.status.ok()))) {
                best_candidate = summary;
            }
            out << "\n" << describe_golomb_rice_candidate_decode(
                stream,
                candidate,
                vector.expected_planes.front(),
                "legacy-gr");
        }
    }
    if (best_candidate.measured) {
        out << "\nlegacy-gr-best byte=" << best_candidate.byte_offset
            << " bit=" << static_cast<int>(best_candidate.bit_offset)
            << " matched_traced_samples=" << best_candidate.matched_samples
            << " output_match=" << best_candidate.output_matches
            << " status: " << describe_status(best_candidate.status);
        if (!best_candidate.first_output_mismatch.empty()) {
            out << "\nlegacy-gr-best-output "
                << best_candidate.first_output_mismatch;
        }
        std::size_t peer_count = 0;
        std::size_t peer_output_match_count = 0;
        out << "\nlegacy-gr-best-peers";
        for (const auto& summary : measured_candidates) {
            if (summary.matched_samples != best_candidate.matched_samples) {
                continue;
            }
            ++peer_count;
            if (summary.output_matches) {
                ++peer_output_match_count;
            }
            if (peer_count <= 8) {
                out << " [" << summary.byte_offset
                    << ":" << static_cast<int>(summary.bit_offset)
                    << (summary.output_matches ? "=out" : "")
                    << "]";
            }
        }
        out << " count=" << peer_count
            << " output_match_count=" << peer_output_match_count;
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
    ASSERT_FALSE(vector.frame_payloads.empty());
    ASSERT_FALSE(vector.expected_planes.empty());
    ASSERT_EQ(vector.frame_payloads.size(), vector.expected_planes.size());

    mffv1::DecoderOptions options;
    options.frame_width = vector.frame_width;
    options.frame_height = vector.frame_height;
    auto decoder = mffv1::create_decoder(options);
    ASSERT_TRUE(decoder.status.ok()) << decoder.status.message;
    ASSERT_NE(decoder.decoder, nullptr);
    if (vector.configuration_record.empty()) {
        const auto bootstrap =
            decoder.decoder->bootstrap_legacy_frame(vector.frame_payloads.front());
        ASSERT_TRUE(bootstrap.status.ok()) << describe_status(bootstrap.status);
        ASSERT_EQ(bootstrap.info.state, mffv1::LegacyBootstrapState::Configured);
    } else {
        const auto configure_status =
            decoder.decoder->configure(vector.configuration_record);
        ASSERT_TRUE(configure_status.ok()) << describe_status(configure_status);
    }

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

std::string environment_setting(const char* name)
{
#if defined(_MSC_VER)
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0
        || value == nullptr) {
        return {};
    }
    std::string setting{value};
    std::free(value);
    return setting;
#else
    const auto* value = std::getenv(name);
    return value == nullptr ? std::string{} : std::string{value};
#endif
}

bool test_vector_flag_setting_enabled(std::string_view setting) noexcept
{
    return !setting.empty() && setting != "0";
}

bool environment_flag_enabled(const char* name)
{
    const auto setting = environment_setting(name);
    return test_vector_flag_setting_enabled(setting);
}

std::string current_test_vector_filter()
{
    return environment_setting("MFFV1_TEST_VECTOR_FILTER");
}

bool trace_successful_legacy_bootstrap()
{
    return environment_flag_enabled("MFFV1_TEST_VECTOR_TRACE_BOOTSTRAP");
}

bool require_all_generated_test_vectors_supported()
{
    return environment_flag_enabled("MFFV1_TEST_VECTOR_REQUIRE_ALL_SUPPORTED");
}

bool try_unsupported_generated_test_vectors()
{
    return environment_flag_enabled("MFFV1_TEST_VECTOR_TRY_UNSUPPORTED");
}

bool matches_test_vector_filter(std::string_view name, std::string_view filter)
{
    return filter.empty() || name.find(filter) != std::string_view::npos;
}

bool has_nonzero_expected_sample(const mffv1_testvectors::DecodeVector& vector)
{
    for (const auto frame_planes : vector.expected_planes) {
        for (const auto& plane : frame_planes) {
            if (std::any_of(
                    plane.samples.begin(),
                    plane.samples.end(),
                    [](std::byte value) { return value != std::byte{0}; })) {
                return true;
            }
        }
    }
    return false;
}

std::string unsupported_decode_vector_reason(
    const mffv1_testvectors::DecodeVector& vector)
{
    if (!vector.configuration_record.empty()) {
        return {};
    }

    if (vector.frame_payloads.empty()) {
        return "legacy vector has no frame payloads";
    }

    mffv1::codec::LegacyFrameBootstrap bootstrap;
    const mffv1::codec::LegacyFrameBootstrapParser bootstrap_parser;
    const auto status = bootstrap_parser.parse(
        vector.frame_payloads.front(),
        vector.frame_width,
        vector.frame_height,
        bootstrap);
    if (!status.ok()) {
        return std::string{"legacy bootstrap parse failed: "} + describe_status(status);
    }
    if (!bootstrap.has_embedded_parameters) {
        return "legacy vector has no embedded parameters";
    }
    if (bootstrap.stream.version == 0
        && bootstrap.stream.entropy_mode == mffv1::EntropyMode::GolombRice) {
        return "legacy version 0 Golomb-Rice payload boundaries are not implemented";
    }
    if (bootstrap.stream.version == 0
        && bootstrap.stream.entropy_mode == mffv1::EntropyMode::Range
        && has_nonzero_expected_sample(vector)) {
        return "legacy version 0 range-coded nonzero sample reconstruction is not implemented";
    }
    return {};
}

bool is_known_unsupported_decode_vector_reason(std::string_view reason)
{
    return reason == "legacy version 0 Golomb-Rice payload boundaries are not implemented"
        || reason == "legacy version 0 range-coded nonzero sample reconstruction is not implemented";
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
    std::size_t matched_count = 0;
    std::size_t decoded_count = 0;
    std::vector<std::string> unsupported_vectors;
    std::vector<std::string> traced_vectors;
    const bool trace_bootstrap = trace_successful_legacy_bootstrap();
    const bool try_unsupported = try_unsupported_generated_test_vectors();
    for (const auto& vector : mffv1_testvectors::decode_vectors()) {
        if (!matches_test_vector_filter(vector.name, filter)) {
            continue;
        }
        ++matched_count;
        const auto unsupported_reason = unsupported_decode_vector_reason(vector);
        if (!unsupported_reason.empty() && !try_unsupported) {
            std::ostringstream entry;
            entry << vector.name << ": " << unsupported_reason;
            if (trace_bootstrap) {
                entry << describe_legacy_bootstrap_state(vector);
            }
            unsupported_vectors.push_back(entry.str());
            continue;
        }
        ++decoded_count;
        expect_decodes_vector(vector);
        if (trace_bootstrap && vector.configuration_record.empty()) {
            std::ostringstream entry;
            entry << vector.name << describe_legacy_bootstrap_state(vector);
            traced_vectors.push_back(entry.str());
        }
    }
    if (matched_count == 0) {
        GTEST_SKIP() << "no generated FFV1 test vectors matched the active filter";
    }
    if (!traced_vectors.empty()) {
        std::ostringstream message;
        message << "matched generated FFV1 legacy test vectors decoded; bootstrap trace follows";
        for (const auto& entry : traced_vectors) {
            message << "\n  " << entry;
        }
        GTEST_SKIP() << message.str();
    }
    if (require_all_generated_test_vectors_supported()
        && !unsupported_vectors.empty()) {
        std::ostringstream message;
        message << "matched generated FFV1 test vectors include unsupported entries";
        for (const auto& entry : unsupported_vectors) {
            message << "\n  " << entry;
        }
        FAIL() << message.str();
    }
    if (decoded_count == 0) {
        std::ostringstream message;
        message << "matched generated FFV1 test vectors are not supported by this build";
        for (const auto& entry : unsupported_vectors) {
            message << "\n  " << entry;
        }
        GTEST_SKIP() << message.str();
    }
#endif
}

TEST(TestVectorTest, EnvironmentFlagSettingsTreatOnlyEmptyAndZeroAsDisabled)
{
#if defined(NO_DEFINE_TEST_VECTOR_DATA)
    GTEST_SKIP() << "external FFV1 test vectors have not been generated";
#else
    EXPECT_FALSE(test_vector_flag_setting_enabled(""));
    EXPECT_FALSE(test_vector_flag_setting_enabled("0"));
    EXPECT_TRUE(test_vector_flag_setting_enabled("1"));
    EXPECT_TRUE(test_vector_flag_setting_enabled("false"));
    EXPECT_TRUE(test_vector_flag_setting_enabled("off"));
#endif
}

TEST(TestVectorTest, UnsupportedGeneratedVectorsAreKnownCompatibilityGaps)
{
#if defined(NO_DEFINE_TEST_VECTOR_DATA)
    GTEST_SKIP() << "external FFV1 test vectors have not been generated";
#else
    std::size_t unsupported_count = 0;
    for (const auto& vector : mffv1_testvectors::decode_vectors()) {
        const auto reason = unsupported_decode_vector_reason(vector);
        if (reason.empty()) {
            continue;
        }
        ++unsupported_count;
        EXPECT_TRUE(is_known_unsupported_decode_vector_reason(reason))
            << vector.name << ": " << reason;
    }
    if (unsupported_count == 0) {
        GTEST_SKIP() << "all generated FFV1 test vectors are currently supported";
    }
#endif
}
