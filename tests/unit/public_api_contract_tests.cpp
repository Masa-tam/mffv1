#include "mffv1/codec.hpp"
#include "mffv1/frame.hpp"
#include "mffv1/result.hpp"

#include <cstdint>
#include <type_traits>

namespace {

static_assert(!std::is_copy_constructible_v<mffv1::IDecoder>);
static_assert(!std::is_copy_assignable_v<mffv1::IDecoder>);
static_assert(!std::is_move_constructible_v<mffv1::IDecoder>);
static_assert(!std::is_move_assignable_v<mffv1::IDecoder>);

static_assert(!std::is_copy_constructible_v<mffv1::IEncoder>);
static_assert(!std::is_copy_assignable_v<mffv1::IEncoder>);
static_assert(!std::is_move_constructible_v<mffv1::IEncoder>);
static_assert(!std::is_move_assignable_v<mffv1::IEncoder>);

static_assert(noexcept(mffv1::ok_status()));

static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::Ok) == 0);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::InvalidArgument)
              == 1);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::InvalidState) == 2);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::UnsupportedFeature)
              == 3);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::SyntaxError) == 4);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::CrcMismatch) == 5);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::ResourceExhausted)
              == 6);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::NotImplemented)
              == 7);
static_assert(static_cast<std::uint32_t>(mffv1::ErrorCode::InternalError) == 8);

static_assert(static_cast<std::uint8_t>(mffv1::PlaneRole::Y) == 0);
static_assert(static_cast<std::uint8_t>(mffv1::PlaneRole::Cb) == 1);
static_assert(static_cast<std::uint8_t>(mffv1::PlaneRole::Cr) == 2);
static_assert(static_cast<std::uint8_t>(mffv1::PlaneRole::Alpha) == 3);
static_assert(static_cast<std::uint8_t>(mffv1::PlaneRole::R) == 4);
static_assert(static_cast<std::uint8_t>(mffv1::PlaneRole::G) == 5);
static_assert(static_cast<std::uint8_t>(mffv1::PlaneRole::B) == 6);

static_assert(static_cast<std::uint8_t>(mffv1::SampleFormat::UInt8) == 0);
static_assert(static_cast<std::uint8_t>(mffv1::SampleFormat::UInt16) == 1);

static_assert(static_cast<std::uint8_t>(
                  mffv1::LegacyBootstrapState::NoEmbeddedParameters)
              == 0);
static_assert(static_cast<std::uint8_t>(mffv1::LegacyBootstrapState::Configured)
              == 1);
static_assert(static_cast<std::uint8_t>(
                  mffv1::LegacyBootstrapState::MatchesCurrentConfiguration)
              == 2);
static_assert(static_cast<std::uint8_t>(
                  mffv1::LegacyBootstrapState::DiffersFromCurrentConfiguration)
              == 3);

} // namespace
