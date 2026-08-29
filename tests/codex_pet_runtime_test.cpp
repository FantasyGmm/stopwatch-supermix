#include "main/apps/app_codex_status/codex_pet_runtime.h"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr size_t kDigestOffset = 40;
constexpr size_t kDigestBytes = 32;
constexpr size_t kFrameTableOffset = 72;
constexpr size_t kFrameEntryBytes = 12;

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
    for (const char value : text) {
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

void redigest(std::vector<uint8_t>& bytes)
{
    assert(kDigestOffset + kDigestBytes <= bytes.size());
    std::fill_n(bytes.begin() + kDigestOffset, kDigestBytes, uint8_t{0});
    std::array<uint8_t, kDigestBytes> digest{};
    assert(mbedtls_sha256(bytes.data(), bytes.size(), digest.data(), 0) == 0);
    std::copy(digest.begin(), digest.end(), bytes.begin() + kDigestOffset);
}

std::vector<uint8_t> with_frame_duration(
    std::vector<uint8_t> bytes,
    uint16_t duration_ms)
{
    write_u16(bytes, kFrameTableOffset, duration_ms);
    write_u16(bytes, kFrameTableOffset + kFrameEntryBytes, duration_ms);
    redigest(bytes);
    return bytes;
}

class FakeFiles final : public codex_pet::PetFileSystem {
public:
    bool ensure_directory() override { return true; }
    bool free_bytes(uint64_t& output) const override
    {
        output = std::numeric_limits<uint64_t>::max();
        return true;
    }
    bool open_staging(uint8_t) override { return false; }
    bool write_at(uint8_t, uint32_t, codex_pet::ByteView) override { return false; }
    bool read_at(
        uint8_t slot,
        uint32_t offset,
        codex_pet::MutableByteView output) const override
    {
        if (slot >= slots.size() || offset > slots[slot].size() ||
            output.size > slots[slot].size() - offset) {
            return false;
        }
        std::copy_n(slots[slot].data() + offset, output.size, output.data);
        return true;
    }
    bool flush(uint8_t) override { return false; }
    bool sync(uint8_t) override { return false; }
    bool close(uint8_t) override { return false; }
    std::optional<size_t> file_size(uint8_t slot) const override
    {
        if (slot >= slots.size() || slots[slot].empty()) {
            return std::nullopt;
        }
        return slots[slot].size();
    }
    bool remove(uint8_t) override { return false; }

    std::array<std::vector<uint8_t>, 2> slots{};
};

class FakeAllocator final : public codex_pet::PetBufferAllocator {
public:
    uint8_t* allocate(size_t bytes) override
    {
        ++allocate_calls;
        if (fail_next) {
            fail_next = false;
            return nullptr;
        }
        uint8_t* buffer = new (std::nothrow) uint8_t[bytes];
        if (buffer != nullptr) {
            ++live_buffers;
            allocations.push_back(bytes);
        }
        return buffer;
    }
    void release(uint8_t* buffer) override
    {
        if (buffer != nullptr) {
            ++release_calls;
            assert(live_buffers != 0);
            --live_buffers;
        }
        delete[] buffer;
    }

    bool fail_next = false;
    size_t allocate_calls = 0;
    size_t release_calls = 0;
    size_t live_buffers = 0;
    std::vector<size_t> allocations{};
};

class FakeDiagnostics final : public codex_pet::PetRuntimeDiagnosticSink {
public:
    void record(codex_pet::PetRuntimeDiagnostic diagnostic) override
    {
        ++count;
        last = diagnostic;
    }

    size_t count = 0;
    codex_pet::PetRuntimeDiagnostic last =
        codex_pet::PetRuntimeDiagnostic::None;
};

class FailingSecondFrameDecoder final : public codex_pet::PetFrameDecoder {
public:
    codex_pet::DecodeError decode(
        const codex_pet::PackageView& package,
        uint16_t frame_index,
        codex_pet::MutableByteView output) override
    {
        if (fail_second_frame && frame_index == 1) {
            return codex_pet::DecodeError::RleOverrun;
        }
        return codex_pet::decode_frame(package, frame_index, output);
    }

    bool fail_second_frame = false;
};

class CountingFrameDecoder final : public codex_pet::PetFrameDecoder {
public:
    codex_pet::DecodeError decode(
        const codex_pet::PackageView& package,
        uint16_t frame_index,
        codex_pet::MutableByteView output) override
    {
        ++calls;
        return codex_pet::decode_frame(package, frame_index, output);
    }

    size_t calls = 0;
};

codex_pet::PetSelection custom_selection(
    uint8_t slot,
    uint32_t generation,
    const std::vector<uint8_t>& bytes)
{
    assert(bytes.size() > kDigestOffset + 32U);
    codex_pet::PetSelection selection{};
    selection.source = codex_pet::Source::Custom;
    selection.slot = slot;
    selection.generation = generation;
    selection.schema = 1;
    std::copy_n(
        bytes.data() + kDigestOffset,
        selection.hash.size(),
        selection.hash.begin());
    selection.usable = true;
    return selection;
}

void install_slot(
    FakeFiles& files,
    uint8_t slot,
    const std::vector<uint8_t>& package)
{
    assert(slot < files.slots.size());
    files.slots[slot].assign(codex_pet::kSlotEnvelopeBytes, 0);
    files.slots[slot].insert(
        files.slots[slot].end(), package.begin(), package.end());
}

void test_builtin_is_the_unconditional_fallback()
{
    FakeFiles files;
    FakeAllocator allocator;
    FakeDiagnostics diagnostics;
    codex_pet::CodexPetRuntime runtime(files, allocator, nullptr, &diagnostics);

    assert(runtime.open({}));
    assert(!runtime.using_custom());
    assert(runtime.current_image() != nullptr);
    assert(runtime.take_frame_changed());
    assert(!runtime.take_frame_changed());
    assert(allocator.live_buffers == 0);
    assert(diagnostics.count == 0);
}

void test_custom_frames_use_exact_deadlines_and_rgb565a8_descriptor(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    FakeDiagnostics diagnostics;
    install_slot(files, 0, package);
    codex_pet::CodexPetRuntime runtime(files, allocator, nullptr, &diagnostics);

    assert(runtime.open(custom_selection(0, 7, package)));
    runtime.set_visible(true, 1000);
    assert(runtime.using_custom());
    const lv_image_dsc_t* image = runtime.current_image();
    assert(image != nullptr);
    assert(image->header.magic == LV_IMAGE_HEADER_MAGIC);
    assert(image->header.cf == LV_COLOR_FORMAT_RGB565A8);
    assert(image->header.w == codex_pet::kWidth);
    assert(image->header.h == codex_pet::kHeight);
    assert(image->header.stride == codex_pet::kWidth * 2U);
    assert(image->data_size == codex_pet::kRgb565A8Bytes);
    assert(image->data != nullptr);
    assert(image->data[0] == 0xffU);
    assert(runtime.take_frame_changed());

    runtime.advance(1179);
    assert(!runtime.take_frame_changed());
    assert(runtime.current_image()->data[0] == 0xffU);
    runtime.advance(1180);
    assert(runtime.take_frame_changed());
    assert(runtime.current_image()->data[0] == 0U);
    runtime.advance(1419);
    assert(!runtime.take_frame_changed());
    runtime.advance(1420);
    assert(runtime.take_frame_changed());
    assert(runtime.current_image()->data[0] == 0xffU);
    assert(diagnostics.count == 0);
}

void test_timer_wrap_pause_screen_off_and_page_navigation_do_not_advance_hidden_time(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    install_slot(files, 0, package);
    codex_pet::CodexPetRuntime runtime(files, allocator);

    assert(runtime.open(custom_selection(0, 7, package)));
    runtime.set_visible(true, 0xfffffff0U);
    assert(runtime.take_frame_changed());
    runtime.advance(0x000000a3U);
    assert(!runtime.take_frame_changed());
    runtime.advance(0x000000a4U);
    assert(runtime.take_frame_changed());

    runtime.set_visible(false, 0x000000a4U);  // Screen off.
    runtime.advance(0x00010000U);
    assert(!runtime.take_frame_changed());
    assert(runtime.current_image()->data[0] == 0U);
    runtime.set_visible(true, 0x00010000U);
    runtime.advance(0x000100efU);
    assert(!runtime.take_frame_changed());
    runtime.advance(0x000100f0U);
    assert(runtime.take_frame_changed());

    runtime.set_visible(false, 0x000100f0U);  // Page navigation.
    runtime.advance(0x00020000U);
    assert(!runtime.take_frame_changed());
}

void test_live_generation_decodes_before_swapping_and_keeps_old_frame_on_allocation_failure(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    install_slot(files, 0, package);
    install_slot(files, 1, package);
    codex_pet::CodexPetRuntime runtime(files, allocator);

    assert(runtime.open(custom_selection(0, 7, package)));
    runtime.set_visible(true, 0);
    const uint8_t* first_frame = runtime.current_image()->data;
    assert(runtime.take_frame_changed());
    assert(runtime.prepare_generation(custom_selection(1, 8, package)));
    assert(runtime.using_custom());
    assert(!runtime.take_frame_changed());
    assert(runtime.current_image()->data == first_frame);
    assert(allocator.live_buffers == 4);
    assert(allocator.release_calls == 0);
    runtime.advance(180);
    assert(runtime.take_frame_changed());
    assert(runtime.current_image()->data != first_frame);
    assert(allocator.live_buffers == 4);
    assert(allocator.release_calls == 0);
    runtime.acknowledge_image_published();
    assert(allocator.live_buffers == 2);
    assert(allocator.release_calls == 2);
    runtime.acknowledge_image_published();
    assert(allocator.release_calls == 2);

    const uint8_t* retained_frame = runtime.current_image()->data;
    allocator.fail_next = true;
    assert(!runtime.prepare_generation(custom_selection(0, 9, package)));
    assert(runtime.current_image()->data == retained_frame);
    assert(!runtime.take_frame_changed());
    assert(allocator.live_buffers == 2);
}

void test_default_restore_and_close_wait_for_lvgl_publication_acknowledgement(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    install_slot(files, 0, package);
    codex_pet::CodexPetRuntime runtime(files, allocator);

    assert(runtime.open(custom_selection(0, 7, package)));
    runtime.set_visible(true, 0);
    assert(runtime.take_frame_changed());
    assert(runtime.prepare_generation({}));
    assert(!runtime.take_frame_changed());
    runtime.advance(180);
    assert(!runtime.using_custom());
    assert(runtime.take_frame_changed());
    assert(allocator.live_buffers == 2);
    assert(allocator.release_calls == 0);
    runtime.acknowledge_image_published();
    assert(allocator.live_buffers == 0);
    assert(allocator.release_calls == 2);
    runtime.acknowledge_image_published();
    assert(allocator.release_calls == 2);

    assert(runtime.open(custom_selection(0, 8, package)));
    runtime.set_visible(true, 0);
    assert(runtime.take_frame_changed());
    assert(runtime.prepare_generation({}));
    runtime.advance(180);
    assert(allocator.live_buffers == 2);
    const size_t releases_before_close = allocator.release_calls;
    runtime.close();
    assert(allocator.live_buffers == 0);
    assert(allocator.release_calls == releases_before_close + 2U);
    runtime.close();
    assert(allocator.release_calls == releases_before_close + 2U);
}

void test_replacing_a_staged_generation_never_accumulates_unpublished_buffers(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    install_slot(files, 0, package);
    install_slot(files, 1, package);
    codex_pet::CodexPetRuntime runtime(files, allocator);

    assert(runtime.open(custom_selection(0, 7, package)));
    runtime.set_visible(true, 0);
    assert(runtime.take_frame_changed());
    assert(runtime.prepare_generation(custom_selection(1, 8, package)));
    assert(allocator.live_buffers == 4);
    assert(runtime.prepare_generation(custom_selection(0, 9, package)));
    assert(allocator.live_buffers == 4);
    assert(allocator.release_calls == 2);
    runtime.advance(180);
    assert(runtime.take_frame_changed());
    assert(allocator.live_buffers == 4);
    runtime.acknowledge_image_published();
    assert(allocator.live_buffers == 2);
    assert(allocator.release_calls == 4);
}

void test_malformed_or_decode_failed_custom_pet_falls_back_once_and_close_releases_once(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    FakeDiagnostics diagnostics;
    install_slot(files, 0, package);
    codex_pet::CodexPetRuntime runtime(files, allocator, nullptr, &diagnostics);

    auto malformed = package;
    malformed[0] = 'X';
    install_slot(files, 1, malformed);
    assert(runtime.open(custom_selection(1, 7, package)));
    assert(!runtime.using_custom());
    assert(diagnostics.count == 1);
    assert(diagnostics.last == codex_pet::PetRuntimeDiagnostic::InvalidPackage);
    runtime.advance(1000);
    assert(diagnostics.count == 1);
    runtime.close();

    FailingSecondFrameDecoder decoder;
    decoder.fail_second_frame = true;
    install_slot(files, 0, package);
    codex_pet::CodexPetRuntime decoding_runtime(
        files, allocator, &decoder, &diagnostics);
    assert(decoding_runtime.open(custom_selection(0, 8, package)));
    decoding_runtime.set_visible(true, 0);
    assert(decoding_runtime.using_custom());
    assert(decoding_runtime.take_frame_changed());
    decoding_runtime.advance(180);
    assert(!decoding_runtime.using_custom());
    assert(diagnostics.count == 2);
    assert(diagnostics.last == codex_pet::PetRuntimeDiagnostic::DecodeFailure);
    assert(allocator.live_buffers == 2);
    decoding_runtime.acknowledge_image_published();
    assert(allocator.live_buffers == 0);

    codex_pet::CodexPetRuntime closing_runtime(files, allocator);
    assert(closing_runtime.open(custom_selection(0, 9, package)));
    assert(allocator.live_buffers == 2);
    const size_t releases_before_close = allocator.release_calls;
    closing_runtime.close();
    assert(allocator.live_buffers == 0);
    assert(allocator.release_calls == releases_before_close + 2U);
    closing_runtime.close();
    assert(allocator.release_calls == releases_before_close + 2U);
}

void test_decode_failure_after_a_staged_swap_keeps_the_original_lvgl_source_alive(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    FailingSecondFrameDecoder decoder;
    install_slot(files, 0, package);
    install_slot(files, 1, package);
    codex_pet::CodexPetRuntime runtime(files, allocator, &decoder);

    assert(runtime.open(custom_selection(0, 7, package)));
    runtime.set_visible(true, 0);
    assert(runtime.take_frame_changed());
    assert(runtime.prepare_generation(custom_selection(1, 8, package)));
    decoder.fail_second_frame = true;
    runtime.advance(360);
    assert(!runtime.using_custom());
    assert(runtime.take_frame_changed());
    assert(allocator.live_buffers == 2);
    runtime.acknowledge_image_published();
    assert(allocator.live_buffers == 0);
}

void test_long_visible_stall_coalesces_frame_cycles_with_bounded_decode_work(
    const std::vector<uint8_t>& package)
{
    const auto sixty_ms_package = with_frame_duration(package, 60);
    FakeFiles files;
    FakeAllocator allocator;
    CountingFrameDecoder decoder;
    install_slot(files, 0, sixty_ms_package);
    codex_pet::CodexPetRuntime runtime(files, allocator, &decoder);

    assert(runtime.open(custom_selection(0, 7, sixty_ms_package)));
    runtime.set_visible(true, 0xfffffff0U);
    assert(runtime.frame_deadline_ms_for_test() == 0x0000002cU);
    const size_t decode_calls_before_stall = decoder.calls;
    runtime.advance(0x0000755cU);
    assert(runtime.take_frame_changed());
    assert(runtime.current_image()->data[0] == 0U);
    assert(runtime.frame_deadline_ms_for_test() == 0x00007598U);
    assert(runtime.next_frame_delay_ms(0x0000755cU) == 60U);
    assert(decoder.calls - decode_calls_before_stall <= codex_pet::kMaxFrames);
    assert(decoder.calls - decode_calls_before_stall == 1U);
}

void test_psram_failure_never_replaces_builtin_fallback(
    const std::vector<uint8_t>& package)
{
    FakeFiles files;
    FakeAllocator allocator;
    FakeDiagnostics diagnostics;
    install_slot(files, 0, package);
    allocator.fail_next = true;
    codex_pet::CodexPetRuntime runtime(files, allocator, nullptr, &diagnostics);

    assert(runtime.open(custom_selection(0, 7, package)));
    assert(!runtime.using_custom());
    assert(runtime.current_image() != nullptr);
    assert(allocator.live_buffers == 0);
    assert(diagnostics.count == 1);
    assert(diagnostics.last == codex_pet::PetRuntimeDiagnostic::AllocationFailure);
}

}  // namespace

int main()
{
    const auto package = read_hex("tests/fixtures/codex_pet/cxp1_minimal.hex");
    test_builtin_is_the_unconditional_fallback();
    test_custom_frames_use_exact_deadlines_and_rgb565a8_descriptor(package);
    test_timer_wrap_pause_screen_off_and_page_navigation_do_not_advance_hidden_time(package);
    test_live_generation_decodes_before_swapping_and_keeps_old_frame_on_allocation_failure(package);
    test_default_restore_and_close_wait_for_lvgl_publication_acknowledgement(package);
    test_replacing_a_staged_generation_never_accumulates_unpublished_buffers(package);
    test_malformed_or_decode_failed_custom_pet_falls_back_once_and_close_releases_once(package);
    test_decode_failure_after_a_staged_swap_keeps_the_original_lvgl_source_alive(package);
    test_psram_failure_never_replaces_builtin_fallback(package);
    test_long_visible_stall_coalesces_frame_cycles_with_bounded_decode_work(package);
    return 0;
}
