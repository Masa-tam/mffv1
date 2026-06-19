#include "util/crc32.hpp"

#include <cstddef>
#include <cstdint>

namespace mffv1::util {

namespace {

constexpr std::uint32_t kPolynomial = 0x04c11db7u;

} // namespace

std::uint32_t crc32_ieee_msb(ByteSpan bytes) noexcept
{
    std::uint32_t remainder = 0;
    for (const std::byte byte : bytes) {
        remainder ^= static_cast<std::uint32_t>(byte) << 24;
        for (int bit = 0; bit < 8; ++bit) {
            if ((remainder & 0x80000000u) != 0) {
                remainder = (remainder << 1) ^ kPolynomial;
            } else {
                remainder <<= 1;
            }
        }
    }
    return remainder;
}

} // namespace mffv1::util
