#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace codex_pet {

constexpr size_t kWidth = 144;
constexpr size_t kHeight = 156;
constexpr size_t kPixelsPerFrame = kWidth * kHeight;
constexpr size_t kRgb565A8Bytes = kPixelsPerFrame * 3;
constexpr size_t kMaxPackageBytes = 256 * 1024;
constexpr size_t kMaxFrames = 24;

struct ByteView {
    const uint8_t* data = nullptr;
    size_t size = 0;
};

struct MutableByteView {
    uint8_t* data = nullptr;
    size_t size = 0;
};

struct FrameView {
    uint16_t duration_ms = 0;
    ByteView compressed{};
};

struct FrameViewList {
    std::array<FrameView, kMaxFrames> entries{};
    size_t count = 0;

    size_t size() const
    {
        return count;
    }

    const FrameView& operator[](size_t index) const
    {
        return entries[index];
    }
};

struct PackageView {
    ByteView encoded{};
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t transparent_index = 0;
    uint16_t palette_count = 0;
    ByteView palette_rgb565{};
    FrameViewList frames{};
};

enum class PackageError {
    None,
    InvalidInput,
    PackageTooLarge,
    InvalidHeader,
    InvalidDimensions,
    InvalidFrameCount,
    InvalidPalette,
    InvalidFlags,
    InvalidLength,
    InvalidDecodedPixelCount,
    InvalidOffsets,
    DigestMismatch,
    InvalidFrameEntry,
    InvalidPayloadSpan,
    PayloadOverlap,
    PayloadTrailingBytes,
    RleOverrun,
    RleUnderrun,
    RleTrailingBytes,
    PaletteIndexOutOfRange,
};

struct ParseResult {
    PackageError error = PackageError::InvalidInput;
    PackageView package{};
};

enum class DecodeError {
    None,
    InvalidPackage,
    FrameOutOfRange,
    InvalidOutput,
    RleOverrun,
    RleUnderrun,
    RleTrailingBytes,
    PaletteIndexOutOfRange,
};

using DecodeResult = DecodeError;

ParseResult parse_package(ByteView bytes);
bool verify_digest(ByteView bytes);
DecodeResult decode_frame(
    const PackageView& package,
    uint16_t frame_index,
    MutableByteView rgb565a8);

}  // namespace codex_pet
