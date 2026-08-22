#include "main/apps/app_codex_status/codex_pet_package.h"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr size_t kDigestOffset = 40;
constexpr size_t kDigestSize = 32;
constexpr size_t kHeaderSize = 72;
constexpr size_t kFrameEntrySize = 12;

uint8_t hex_nibble(char value)
{
    assert(std::isxdigit(static_cast<unsigned char>(value)) != 0);
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
    assert(value >= 'a' && value <= 'f');
    return static_cast<uint8_t>(value - 'a' + 10);
}

std::vector<uint8_t> read_hex(const char* path)
{
    std::ifstream input(path);
    assert(input.good());
    const std::string text(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    std::string hex;
    for (char value : text) {
        if (std::isspace(static_cast<unsigned char>(value)) == 0) {
            hex.push_back(value);
        }
    }
    assert(!hex.empty());
    assert((hex.size() % 2U) == 0U);
    std::vector<uint8_t> bytes(hex.size() / 2U);
    for (size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<uint8_t>(
            (hex_nibble(hex[index * 2U]) << 4U) |
            hex_nibble(hex[index * 2U + 1U]));
    }
    return bytes;
}

void write_u16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value)
{
    assert(offset <= bytes.size() && 2U <= bytes.size() - offset);
    bytes[offset] = static_cast<uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value)
{
    assert(offset <= bytes.size() && 4U <= bytes.size() - offset);
    bytes[offset] = static_cast<uint8_t>(value & 0xffU);
    bytes[offset + 1U] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 2U] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 3U] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

void redigest(std::vector<uint8_t>& bytes)
{
    assert(kDigestOffset + kDigestSize <= bytes.size());
    std::fill_n(bytes.begin() + kDigestOffset, kDigestSize, uint8_t{0});
    std::array<uint8_t, kDigestSize> digest{};
    assert(mbedtls_sha256(bytes.data(), bytes.size(), digest.data(), 0) == 0);
    std::copy(digest.begin(), digest.end(), bytes.begin() + kDigestOffset);
}

codex_pet::ParseResult parse(const std::vector<uint8_t>& bytes)
{
    return codex_pet::parse_package({bytes.data(), bytes.size()});
}

void expect_parse_error(const std::vector<uint8_t>& bytes)
{
    assert(parse(bytes).error != codex_pet::PackageError::None);
}

template <typename Mutation>
void expect_redigested_parse_error(
    const std::vector<uint8_t>& valid,
    Mutation mutation)
{
    auto malformed = valid;
    mutation(malformed);
    redigest(malformed);
    expect_parse_error(malformed);
}

void test_fixed_vector_and_decode(const std::vector<uint8_t>& bytes)
{
    assert(codex_pet::verify_digest({bytes.data(), bytes.size()}));
    const auto parsed = parse(bytes);
    assert(parsed.error == codex_pet::PackageError::None);
    assert(parsed.package.width == 144);
    assert(parsed.package.height == 156);
    assert(parsed.package.frames.size() == 2);
    assert(parsed.package.frames[0].duration_ms == 180);
    assert(parsed.package.frames[1].duration_ms == 240);

    std::array<uint8_t, codex_pet::kRgb565A8Bytes> output{};
    assert(
        codex_pet::decode_frame(
            parsed.package, 0, {output.data(), output.size()}) ==
        codex_pet::DecodeError::None);
    assert(std::all_of(
        output.begin(), output.end(), [](uint8_t value) { return value == 0xffU; }));

    output.fill(0xaaU);
    assert(
        codex_pet::decode_frame(
            parsed.package, 1, {output.data(), output.size()}) ==
        codex_pet::DecodeError::None);
    assert(std::all_of(
        output.begin(), output.end(), [](uint8_t value) { return value == 0U; }));

    output.fill(0xaaU);
    assert(
        codex_pet::decode_frame(
            parsed.package,
            0,
            {output.data(), output.size() - 1U}) ==
        codex_pet::DecodeError::InvalidOutput);
    assert(std::all_of(
        output.begin(), output.end(), [](uint8_t value) { return value == 0xaaU; }));
    assert(
        codex_pet::decode_frame(
            parsed.package, 2, {output.data(), output.size()}) ==
        codex_pet::DecodeError::FrameOutOfRange);
}

void test_every_multibyte_header_field(const std::vector<uint8_t>& valid)
{
    const std::array<std::pair<size_t, uint16_t>, 8> u16_mutations = {{
        {4, 2},
        {6, 71},
        {8, 143},
        {10, 155},
        {12, 0},
        {14, 0},
        {16, 0},
        {18, 1},
    }};
    for (const auto& mutation : u16_mutations) {
        expect_redigested_parse_error(valid, [&](std::vector<uint8_t>& bytes) {
            write_u16(bytes, mutation.first, mutation.second);
        });
    }

    const std::array<std::pair<size_t, uint32_t>, 5> u32_mutations = {{
        {20, 71},
        {24, 95},
        {28, 97},
        {32, static_cast<uint32_t>(valid.size() - 1U)},
        {36, 44927},
    }};
    for (const auto& mutation : u32_mutations) {
        expect_redigested_parse_error(valid, [&](std::vector<uint8_t>& bytes) {
            write_u32(bytes, mutation.first, mutation.second);
        });
    }
}

void test_every_multibyte_frame_field(const std::vector<uint8_t>& valid)
{
    for (size_t frame = 0; frame < 2; ++frame) {
        const size_t entry = kHeaderSize + frame * kFrameEntrySize;
        expect_redigested_parse_error(valid, [&](std::vector<uint8_t>& bytes) {
            write_u16(bytes, entry, 59);
        });
        expect_redigested_parse_error(valid, [&](std::vector<uint8_t>& bytes) {
            write_u16(bytes, entry + 2U, 1);
        });
        expect_redigested_parse_error(valid, [&](std::vector<uint8_t>& bytes) {
            write_u32(bytes, entry + 4U, 0xffffffffU);
        });
        expect_redigested_parse_error(valid, [&](std::vector<uint8_t>& bytes) {
            write_u32(bytes, entry + 8U, 0);
        });
    }
}

void test_untrusted_spans_and_products(const std::vector<uint8_t>& valid)
{
    expect_redigested_parse_error(valid, [](std::vector<uint8_t>& bytes) {
        write_u16(bytes, 8, 0xffffU);
        write_u16(bytes, 10, 0xffffU);
        write_u16(bytes, 12, 0xffffU);
        write_u32(bytes, 36, 0xffffffffU);
    });

    expect_redigested_parse_error(valid, [](std::vector<uint8_t>& bytes) {
        write_u32(bytes, kHeaderSize + kFrameEntrySize + 4U, 98);
    });

    expect_redigested_parse_error(valid, [](std::vector<uint8_t>& bytes) {
        write_u16(bytes, kHeaderSize + 2U, 1);
    });

    expect_redigested_parse_error(valid, [](std::vector<uint8_t>& bytes) {
        bytes[99] = 2;
    });
}

void test_rle_failures(const std::vector<uint8_t>& valid)
{
    constexpr size_t first_payload = 98;
    constexpr size_t last_control = first_payload + 344;

    expect_redigested_parse_error(valid, [](std::vector<uint8_t>& bytes) {
        bytes[last_control] = 0x99U;
    });
    expect_redigested_parse_error(valid, [](std::vector<uint8_t>& bytes) {
        bytes[last_control] = 0x9bU;
    });
    expect_redigested_parse_error(valid, [](std::vector<uint8_t>& bytes) {
        write_u32(bytes, kHeaderSize + 8U, 347);
        write_u32(bytes, kHeaderSize + kFrameEntrySize + 4U, 445);
        write_u32(bytes, kHeaderSize + kFrameEntrySize + 8U, 345);
    });
}

void test_digest_mismatch(const std::vector<uint8_t>& valid)
{
    auto malformed = valid;
    malformed.back() ^= 1U;
    assert(!codex_pet::verify_digest({malformed.data(), malformed.size()}));
    expect_parse_error(malformed);
}

}  // namespace

int main()
{
    const auto bytes = read_hex("tests/fixtures/codex_pet/cxp1_minimal.hex");
    test_fixed_vector_and_decode(bytes);
    test_every_multibyte_header_field(bytes);
    test_every_multibyte_frame_field(bytes);
    test_untrusted_spans_and_products(bytes);
    test_rle_failures(bytes);
    test_digest_mismatch(bytes);
    return 0;
}
