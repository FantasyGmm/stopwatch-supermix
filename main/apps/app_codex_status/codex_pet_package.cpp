#include "codex_pet_package.h"

#include <mbedtls/sha256.h>

#include <array>

namespace codex_pet {
namespace {

constexpr size_t kHeaderSize = 72;
constexpr size_t kDigestOffset = 40;
constexpr size_t kDigestSize = 32;
constexpr size_t kFrameEntrySize = 12;
constexpr uint16_t kSchemaVersion = 1;
constexpr uint16_t kMinimumDurationMs = 60;
constexpr uint16_t kMaximumDurationMs = 1000;
constexpr size_t kMaximumDecodedPixels = 64 * 1024 * 1024;

enum class StreamError {
    None,
    Overrun,
    Underrun,
    TrailingBytes,
    PaletteIndexOutOfRange,
};

bool range_within(size_t offset, size_t length, size_t size)
{
    return offset <= size && length <= size - offset;
}

bool checked_add(size_t left, size_t right, size_t& result)
{
    if (left > static_cast<size_t>(-1) - right) {
        return false;
    }
    result = left + right;
    return true;
}

bool checked_multiply(size_t left, size_t right, size_t& result)
{
    if (left != 0 && right > static_cast<size_t>(-1) / left) {
        return false;
    }
    result = left * right;
    return true;
}

bool read_u16(ByteView bytes, size_t offset, uint16_t& value)
{
    if (bytes.data == nullptr || !range_within(offset, 2, bytes.size)) {
        return false;
    }
    value = static_cast<uint16_t>(bytes.data[offset]) |
            static_cast<uint16_t>(bytes.data[offset + 1U]) << 8U;
    return true;
}

bool read_u32(ByteView bytes, size_t offset, uint32_t& value)
{
    if (bytes.data == nullptr || !range_within(offset, 4, bytes.size)) {
        return false;
    }
    value = static_cast<uint32_t>(bytes.data[offset]) |
            static_cast<uint32_t>(bytes.data[offset + 1U]) << 8U |
            static_cast<uint32_t>(bytes.data[offset + 2U]) << 16U |
            static_cast<uint32_t>(bytes.data[offset + 3U]) << 24U;
    return true;
}

StreamError validate_stream(
    ByteView compressed,
    size_t expected_size,
    uint16_t transparent_index)
{
    if (compressed.data == nullptr) {
        return StreamError::Overrun;
    }

    size_t cursor = 0;
    size_t produced = 0;
    while (cursor < compressed.size) {
        if (produced == expected_size) {
            return StreamError::TrailingBytes;
        }

        const uint8_t control = compressed.data[cursor];
        ++cursor;
        if (control <= 0x7fU) {
            const size_t count = static_cast<size_t>(control) + 1U;
            if (count > compressed.size - cursor ||
                count > expected_size - produced) {
                return StreamError::Overrun;
            }
            for (size_t index = 0; index < count; ++index) {
                if (compressed.data[cursor + index] > transparent_index) {
                    return StreamError::PaletteIndexOutOfRange;
                }
            }
            cursor += count;
            produced += count;
        } else {
            const size_t count = 258U - static_cast<size_t>(control);
            if (cursor >= compressed.size || count > expected_size - produced) {
                return StreamError::Overrun;
            }
            if (compressed.data[cursor] > transparent_index) {
                return StreamError::PaletteIndexOutOfRange;
            }
            ++cursor;
            produced += count;
        }
    }

    return produced == expected_size ? StreamError::None : StreamError::Underrun;
}

PackageError package_error(StreamError error)
{
    switch (error) {
    case StreamError::None:
        return PackageError::None;
    case StreamError::Overrun:
        return PackageError::RleOverrun;
    case StreamError::Underrun:
        return PackageError::RleUnderrun;
    case StreamError::TrailingBytes:
        return PackageError::RleTrailingBytes;
    case StreamError::PaletteIndexOutOfRange:
        return PackageError::PaletteIndexOutOfRange;
    }
    return PackageError::InvalidInput;
}

DecodeError decode_error(StreamError error)
{
    switch (error) {
    case StreamError::None:
        return DecodeError::None;
    case StreamError::Overrun:
        return DecodeError::RleOverrun;
    case StreamError::Underrun:
        return DecodeError::RleUnderrun;
    case StreamError::TrailingBytes:
        return DecodeError::RleTrailingBytes;
    case StreamError::PaletteIndexOutOfRange:
        return DecodeError::PaletteIndexOutOfRange;
    }
    return DecodeError::InvalidPackage;
}

void write_pixel(
    const PackageView& package,
    size_t pixel,
    uint8_t palette_index,
    MutableByteView output)
{
    const size_t color_offset = pixel * 2U;
    const size_t alpha_offset = kPixelsPerFrame * 2U + pixel;
    if (palette_index == package.transparent_index) {
        output.data[color_offset] = 0;
        output.data[color_offset + 1U] = 0;
        output.data[alpha_offset] = 0;
        return;
    }

    const size_t palette_offset = static_cast<size_t>(palette_index) * 2U;
    output.data[color_offset] = package.palette_rgb565.data[palette_offset];
    output.data[color_offset + 1U] =
        package.palette_rgb565.data[palette_offset + 1U];
    output.data[alpha_offset] = 0xffU;
}

}  // namespace

bool verify_digest(ByteView bytes)
{
    if (bytes.data == nullptr || bytes.size < kHeaderSize ||
        bytes.size > kMaxPackageBytes) {
        return false;
    }

    std::array<uint8_t, kDigestSize> calculated{};
    constexpr std::array<uint8_t, kDigestSize> zeros{};
    mbedtls_sha256_context context;
    mbedtls_sha256_init(&context);
    const bool hashed =
        mbedtls_sha256_starts(&context, 0) == 0 &&
        mbedtls_sha256_update(
            &context, bytes.data, kDigestOffset) == 0 &&
        mbedtls_sha256_update(
            &context, zeros.data(), zeros.size()) == 0 &&
        mbedtls_sha256_update(
            &context,
            bytes.data + kDigestOffset + kDigestSize,
            bytes.size - kDigestOffset - kDigestSize) == 0 &&
        mbedtls_sha256_finish(&context, calculated.data()) == 0;
    mbedtls_sha256_free(&context);
    if (!hashed) {
        return false;
    }

    uint8_t difference = 0;
    for (size_t index = 0; index < calculated.size(); ++index) {
        difference = static_cast<uint8_t>(
            difference |
            (calculated[index] ^ bytes.data[kDigestOffset + index]));
    }
    return difference == 0;
}

ParseResult parse_package(ByteView bytes)
{
    if (bytes.data == nullptr || bytes.size < kHeaderSize) {
        return {PackageError::InvalidInput, {}};
    }
    if (bytes.size > kMaxPackageBytes) {
        return {PackageError::PackageTooLarge, {}};
    }
    if (bytes.data[0] != 'C' || bytes.data[1] != 'X' ||
        bytes.data[2] != 'P' || bytes.data[3] != '1') {
        return {PackageError::InvalidHeader, {}};
    }

    uint16_t schema = 0;
    uint16_t header_size = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t frame_count = 0;
    uint16_t palette_count = 0;
    uint16_t transparent_index = 0;
    uint16_t flags = 0;
    uint32_t frame_table_offset = 0;
    uint32_t palette_offset = 0;
    uint32_t payload_offset = 0;
    uint32_t total_length = 0;
    uint32_t decoded_pixel_count = 0;
    if (!read_u16(bytes, 4, schema) ||
        !read_u16(bytes, 6, header_size) ||
        !read_u16(bytes, 8, width) ||
        !read_u16(bytes, 10, height) ||
        !read_u16(bytes, 12, frame_count) ||
        !read_u16(bytes, 14, palette_count) ||
        !read_u16(bytes, 16, transparent_index) ||
        !read_u16(bytes, 18, flags) ||
        !read_u32(bytes, 20, frame_table_offset) ||
        !read_u32(bytes, 24, palette_offset) ||
        !read_u32(bytes, 28, payload_offset) ||
        !read_u32(bytes, 32, total_length) ||
        !read_u32(bytes, 36, decoded_pixel_count)) {
        return {PackageError::InvalidHeader, {}};
    }

    if (schema != kSchemaVersion || header_size != kHeaderSize) {
        return {PackageError::InvalidHeader, {}};
    }
    if (width != kWidth || height != kHeight) {
        return {PackageError::InvalidDimensions, {}};
    }
    if (frame_count == 0 || frame_count > kMaxFrames) {
        return {PackageError::InvalidFrameCount, {}};
    }
    if (palette_count == 0 || palette_count > 255 ||
        transparent_index != palette_count) {
        return {PackageError::InvalidPalette, {}};
    }
    if (flags != 0) {
        return {PackageError::InvalidFlags, {}};
    }
    if (total_length != bytes.size || total_length > kMaxPackageBytes) {
        return {PackageError::InvalidLength, {}};
    }

    size_t pixels = 0;
    size_t all_pixels = 0;
    if (!checked_multiply(width, height, pixels) || pixels == 0 ||
        !checked_multiply(pixels, frame_count, all_pixels) ||
        all_pixels > kMaximumDecodedPixels ||
        decoded_pixel_count != all_pixels) {
        return {PackageError::InvalidDecodedPixelCount, {}};
    }

    size_t frame_table_bytes = 0;
    size_t frame_table_end = 0;
    size_t palette_bytes = 0;
    size_t palette_end = 0;
    if (!checked_multiply(frame_count, kFrameEntrySize, frame_table_bytes) ||
        !checked_add(frame_table_offset, frame_table_bytes, frame_table_end) ||
        !checked_multiply(palette_count, 2U, palette_bytes) ||
        !checked_add(palette_offset, palette_bytes, palette_end) ||
        frame_table_offset != kHeaderSize ||
        palette_offset != frame_table_end ||
        payload_offset != palette_end ||
        !range_within(frame_table_offset, frame_table_bytes, total_length) ||
        !range_within(palette_offset, palette_bytes, total_length) ||
        payload_offset > total_length) {
        return {PackageError::InvalidOffsets, {}};
    }
    if (!verify_digest(bytes)) {
        return {PackageError::DigestMismatch, {}};
    }

    PackageView package{};
    package.encoded = bytes;
    package.width = width;
    package.height = height;
    package.transparent_index = transparent_index;
    package.palette_count = palette_count;
    package.palette_rgb565 = {
        bytes.data + static_cast<size_t>(palette_offset), palette_bytes};

    size_t previous_end = payload_offset;
    for (size_t index = 0; index < frame_count; ++index) {
        size_t entry_delta = 0;
        size_t entry_offset = 0;
        if (!checked_multiply(index, kFrameEntrySize, entry_delta) ||
            !checked_add(frame_table_offset, entry_delta, entry_offset) ||
            !range_within(entry_offset, kFrameEntrySize, total_length)) {
            return {PackageError::InvalidOffsets, {}};
        }

        uint16_t duration_ms = 0;
        uint16_t reserved = 0;
        uint32_t frame_offset = 0;
        uint32_t frame_length = 0;
        if (!read_u16(bytes, entry_offset, duration_ms) ||
            !read_u16(bytes, entry_offset + 2U, reserved) ||
            !read_u32(bytes, entry_offset + 4U, frame_offset) ||
            !read_u32(bytes, entry_offset + 8U, frame_length) ||
            reserved != 0 || duration_ms < kMinimumDurationMs ||
            duration_ms > kMaximumDurationMs) {
            return {PackageError::InvalidFrameEntry, {}};
        }
        if (frame_length == 0 || frame_offset < payload_offset ||
            !range_within(frame_offset, frame_length, total_length)) {
            return {PackageError::InvalidPayloadSpan, {}};
        }
        if (frame_offset < previous_end) {
            return {PackageError::PayloadOverlap, {}};
        }

        size_t frame_end = 0;
        if (!checked_add(frame_offset, frame_length, frame_end)) {
            return {PackageError::InvalidPayloadSpan, {}};
        }
        const ByteView compressed{
            bytes.data + static_cast<size_t>(frame_offset),
            static_cast<size_t>(frame_length),
        };
        const StreamError stream_error =
            validate_stream(compressed, pixels, transparent_index);
        if (stream_error != StreamError::None) {
            return {package_error(stream_error), {}};
        }

        package.frames.entries[index] = {duration_ms, compressed};
        package.frames.count = index + 1U;
        previous_end = frame_end;
    }
    if (previous_end != total_length) {
        return {PackageError::PayloadTrailingBytes, {}};
    }

    return {PackageError::None, package};
}

DecodeResult decode_frame(
    const PackageView& package,
    uint16_t frame_index,
    MutableByteView output)
{
    if (output.data == nullptr || output.size != kRgb565A8Bytes) {
        return DecodeError::InvalidOutput;
    }
    if (frame_index >= package.frames.size()) {
        return DecodeError::FrameOutOfRange;
    }

    size_t palette_bytes = 0;
    if (package.width != kWidth || package.height != kHeight ||
        package.palette_count == 0 || package.palette_count > 255 ||
        package.transparent_index != package.palette_count ||
        !checked_multiply(package.palette_count, 2U, palette_bytes) ||
        package.palette_rgb565.data == nullptr ||
        package.palette_rgb565.size != palette_bytes) {
        return DecodeError::InvalidPackage;
    }

    const FrameView& frame = package.frames[frame_index];
    const StreamError stream_error = validate_stream(
        frame.compressed, kPixelsPerFrame, package.transparent_index);
    if (stream_error != StreamError::None) {
        return decode_error(stream_error);
    }

    size_t cursor = 0;
    size_t produced = 0;
    while (cursor < frame.compressed.size) {
        const uint8_t control = frame.compressed.data[cursor];
        ++cursor;
        if (control <= 0x7fU) {
            const size_t count = static_cast<size_t>(control) + 1U;
            for (size_t index = 0; index < count; ++index) {
                write_pixel(
                    package,
                    produced,
                    frame.compressed.data[cursor + index],
                    output);
                ++produced;
            }
            cursor += count;
        } else {
            const size_t count = 258U - static_cast<size_t>(control);
            const uint8_t palette_index = frame.compressed.data[cursor];
            ++cursor;
            for (size_t index = 0; index < count; ++index) {
                write_pixel(package, produced, palette_index, output);
                ++produced;
            }
        }
    }
    return DecodeError::None;
}

}  // namespace codex_pet
