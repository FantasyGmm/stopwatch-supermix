#include "main/apps/app_codex_status/codex_pet_store.h"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr size_t kDigestOffset = 40;
constexpr size_t kDigestSize = 32;
constexpr size_t kFirstFrameDurationOffset = 72;

uint8_t hex_nibble(char value)
{
    assert(std::isxdigit(static_cast<unsigned char>(value)) != 0);
    if (value >= '0' && value <= '9') {
        return static_cast<uint8_t>(value - '0');
    }
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
    assert(!hex.empty() && (hex.size() % 2U) == 0U);
    std::vector<uint8_t> bytes(hex.size() / 2U);
    for (size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<uint8_t>(
            (hex_nibble(hex[index * 2U]) << 4U) |
            hex_nibble(hex[index * 2U + 1U]));
    }
    return bytes;
}

uint16_t read_u16(const uint8_t* bytes)
{
    return static_cast<uint16_t>(bytes[0]) |
           static_cast<uint16_t>(bytes[1]) << 8U;
}

uint32_t read_u32(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0]) |
           static_cast<uint32_t>(bytes[1]) << 8U |
           static_cast<uint32_t>(bytes[2]) << 16U |
           static_cast<uint32_t>(bytes[3]) << 24U;
}

void write_u16(uint8_t* bytes, uint16_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
}

void write_u32(uint8_t* bytes, uint32_t value)
{
    bytes[0] = static_cast<uint8_t>(value & 0xffU);
    bytes[1] = static_cast<uint8_t>((value >> 8U) & 0xffU);
    bytes[2] = static_cast<uint8_t>((value >> 16U) & 0xffU);
    bytes[3] = static_cast<uint8_t>((value >> 24U) & 0xffU);
}

uint32_t crc32(const uint8_t* bytes, size_t size)
{
    uint32_t crc = 0xffffffffU;
    for (size_t index = 0; index < size; ++index) {
        crc ^= bytes[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask =
                static_cast<uint32_t>(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

void redigest(std::vector<uint8_t>& bytes)
{
    assert(kDigestOffset + kDigestSize <= bytes.size());
    std::fill_n(bytes.begin() + kDigestOffset, kDigestSize, uint8_t{0});
    std::array<uint8_t, kDigestSize> digest{};
    assert(mbedtls_sha256(bytes.data(), bytes.size(), digest.data(), 0) == 0);
    std::copy(digest.begin(), digest.end(), bytes.begin() + kDigestOffset);
}

std::vector<uint8_t> package_variant(
    const std::vector<uint8_t>& base,
    uint16_t duration_ms)
{
    auto result = base;
    write_u16(result.data() + kFirstFrameDurationOffset, duration_ms);
    redigest(result);
    assert(codex_pet::verify_digest({result.data(), result.size()}));
    assert(
        codex_pet::parse_package({result.data(), result.size()}).error ==
        codex_pet::PackageError::None);
    return result;
}

codex_pet::PetMetadata metadata(const std::vector<uint8_t>& package)
{
    codex_pet::PetMetadata result{};
    result.schema = read_u16(package.data() + 4);
    result.width = read_u16(package.data() + 8);
    result.height = read_u16(package.data() + 10);
    result.frame_count = read_u16(package.data() + 12);
    std::copy_n(
        package.data() + kDigestOffset,
        result.hash.size(),
        result.hash.begin());
    return result;
}

struct Failure {
    std::string operation;
    size_t occurrence = 1;
    size_t calls = 0;
    bool triggered = false;

    bool hit(const char* candidate)
    {
        if (operation != candidate) {
            return false;
        }
        ++calls;
        if (calls == occurrence) {
            triggered = true;
            return true;
        }
        return false;
    }
};

struct PowerLoss {
};

struct CrashAfter {
    std::string operation;
    size_t occurrence = 1;
    size_t calls = 0;
    bool triggered = false;

    void hit(const char* candidate)
    {
        if (operation != candidate) {
            return;
        }
        ++calls;
        if (calls == occurrence) {
            triggered = true;
            throw PowerLoss{};
        }
    }
};

class FakeFileSystem final : public codex_pet::PetFileSystem {
public:
    bool ensure_directory() override
    {
        events.emplace_back("ensure_directory");
        if (failure.hit("ensure_directory")) {
            return false;
        }
        crash.hit("ensure_directory");
        return true;
    }

    bool free_bytes(uint64_t& output) const override
    {
        events.emplace_back("free_bytes");
        if (failure.hit("free_bytes")) {
            return false;
        }
        output = available;
        crash.hit("free_bytes");
        return true;
    }

    bool open_staging(uint8_t slot) override
    {
        events.emplace_back("open_staging");
        if (slot > 1 || failure.hit("open_staging")) {
            return false;
        }
        files[slot].clear();
        present[slot] = true;
        open[slot] = true;
        crash.hit("open_staging");
        return true;
    }

    bool write_at(
        uint8_t slot,
        uint32_t offset,
        codex_pet::ByteView bytes) override
    {
        const char* operation =
            offset < codex_pet::kSlotEnvelopeBytes ?
                "write_envelope" : "write_chunk";
        events.emplace_back(operation);
        if (slot > 1 || !open[slot] || bytes.data == nullptr ||
            failure.hit(operation)) {
            return false;
        }
        const size_t end = static_cast<size_t>(offset) + bytes.size;
        if (end < offset) {
            return false;
        }
        if (files[slot].size() < end) {
            files[slot].resize(end);
        }
        std::copy_n(bytes.data, bytes.size, files[slot].begin() + offset);
        crash.hit(operation);
        return true;
    }

    bool read_at(
        uint8_t slot,
        uint32_t offset,
        codex_pet::MutableByteView output) const override
    {
        const char* operation =
            offset == 0 && output.size == codex_pet::kSlotEnvelopeBytes ?
                "read_envelope" : "read_package";
        events.emplace_back(operation);
        if (slot > 1 || output.data == nullptr || failure.hit(operation) ||
            offset > files[slot].size() ||
            output.size > files[slot].size() - offset) {
            return false;
        }
        std::copy_n(files[slot].data() + offset, output.size, output.data);
        crash.hit(operation);
        return true;
    }

    bool flush(uint8_t slot) override
    {
        events.emplace_back("flush");
        if (slot > 1 || !open[slot] || failure.hit("flush")) {
            return false;
        }
        crash.hit("flush");
        return true;
    }

    bool sync(uint8_t slot) override
    {
        events.emplace_back("sync");
        if (slot > 1 || !open[slot] || failure.hit("sync")) {
            return false;
        }
        crash.hit("sync");
        return true;
    }

    bool close(uint8_t slot) override
    {
        events.emplace_back("close");
        if (slot > 1 || !open[slot]) {
            return false;
        }
        open[slot] = false;
        if (failure.hit("close")) {
            return false;
        }
        crash.hit("close");
        return true;
    }

    std::optional<size_t> file_size(uint8_t slot) const override
    {
        events.emplace_back("file_size");
        if (slot > 1 || failure.hit("file_size") || !present[slot]) {
            return std::nullopt;
        }
        const size_t result = files[slot].size();
        crash.hit("file_size");
        return result;
    }

    bool remove(uint8_t slot) override
    {
        events.emplace_back("remove");
        removed_slots.push_back(slot);
        if (slot > 1 || failure.hit("remove")) {
            return false;
        }
        files[slot].clear();
        present[slot] = false;
        open[slot] = false;
        crash.hit("remove");
        return true;
    }

    mutable Failure failure{};
    mutable CrashAfter crash{};
    mutable std::vector<std::string> events{};
    mutable std::vector<uint8_t> removed_slots{};
    uint64_t available = std::numeric_limits<uint64_t>::max();
    std::array<std::vector<uint8_t>, 2> files{};
    std::array<bool, 2> present{};
    std::array<bool, 2> open{};
};

class FakeAllocator final : public codex_pet::PetBufferAllocator {
public:
    uint8_t* allocate(size_t bytes) override
    {
        requests.push_back(bytes);
        if (failure.hit("allocate")) {
            return nullptr;
        }
        storage.resize(bytes);
        ++live;
        maximum_live = std::max(maximum_live, live);
        crash.hit("allocate");
        return storage.data();
    }

    void release(uint8_t* buffer) override
    {
        assert(buffer == storage.data());
        assert(live == 1);
        --live;
    }

    Failure failure{};
    CrashAfter crash{};
    std::vector<size_t> requests{};
    std::vector<uint8_t> storage{};
    size_t live = 0;
    size_t maximum_live = 0;
};

class FakeSelectionStore final : public codex_pet::PetSelectionStore {
public:
    codex_pet::SelectionIoResult load(
        codex_pet::SelectionRecord& output) const override
    {
        if (failure.hit("nvs_load")) {
            return codex_pet::SelectionIoResult::Error;
        }
        if (!persistent.has_value()) {
            return codex_pet::SelectionIoResult::NotFound;
        }
        output = *persistent;
        crash.hit("nvs_load");
        return codex_pet::SelectionIoResult::Ok;
    }

    bool stage(const codex_pet::SelectionRecord& record) override
    {
        crash_before.hit("nvs_stage");
        if (failure.hit("nvs_stage")) {
            return false;
        }
        pending = record;
        crash.hit("nvs_stage");
        return true;
    }

    bool commit() override
    {
        if (failure.hit("nvs_commit") || !pending.has_value()) {
            return false;
        }
        persistent = pending;
        pending.reset();
        crash.hit("nvs_commit");
        return true;
    }

    mutable Failure failure{};
    mutable CrashAfter crash_before{};
    mutable CrashAfter crash{};
    std::optional<codex_pet::SelectionRecord> persistent{};
    std::optional<codex_pet::SelectionRecord> pending{};
};

struct Rig {
    FakeFileSystem fs{};
    FakeAllocator allocator{};
    FakeSelectionStore selections{};
};

codex_pet::PetSelection reboot(Rig& rig)
{
    codex_pet::CodexPetStore store(rig.fs, rig.allocator, rig.selections);
    return store.load_selection();
}

codex_pet::StoreResult install(
    Rig& rig,
    const std::vector<uint8_t>& package,
    bool acknowledge = true)
{
    codex_pet::CodexPetStore store(rig.fs, rig.allocator, rig.selections);
    const auto begin = store.begin(
        static_cast<uint32_t>(package.size()), metadata(package));
    if (begin != codex_pet::StoreResult::Ok) {
        return begin;
    }
    const size_t split = package.size() / 2U;
    auto result = store.write(
        0,
        {package.data(), split});
    if (result != codex_pet::StoreResult::Ok) {
        return result;
    }
    result = store.write(
        static_cast<uint32_t>(split),
        {package.data() + split, package.size() - split});
    if (result != codex_pet::StoreResult::Ok) {
        return result;
    }
    result = store.commit();
    if (result == codex_pet::StoreResult::Ok && acknowledge) {
        const auto cleanup = store.acknowledge();
        if (cleanup != codex_pet::StoreResult::Ok) {
            return cleanup;
        }
    }
    return result;
}

void install_generations(
    Rig& rig,
    const std::vector<uint8_t>& base,
    uint32_t count)
{
    for (uint32_t generation = 1; generation <= count; ++generation) {
        const auto package = package_variant(
            base,
            static_cast<uint16_t>(180U + generation));
        assert(install(rig, package) == codex_pet::StoreResult::Ok);
        const auto selected = reboot(rig);
        assert(selected.source == codex_pet::Source::Custom);
        assert(selected.generation == generation);
    }
}

void test_commit_fault_matrix_keeps_only_complete_generations(
    const std::vector<uint8_t>& base)
{
    Rig installed{};
    install_generations(installed, base, 4);
    const auto old_package = package_variant(base, 184);
    const auto next_package = package_variant(base, 185);
    const auto old_hash = metadata(old_package).hash;
    const auto next_hash = metadata(next_package).hash;

    struct Case {
        enum class Port { File, Allocator, Selection } port;
        const char* operation;
        size_t occurrence;
    };
    const std::array<Case, 21> cases = {{
        {Case::Port::File, "ensure_directory", 1},
        {Case::Port::Selection, "nvs_load", 1},
        {Case::Port::File, "file_size", 1},
        {Case::Port::File, "file_size", 3},
        {Case::Port::File, "remove", 1},
        {Case::Port::File, "free_bytes", 1},
        {Case::Port::File, "open_staging", 1},
        {Case::Port::File, "write_envelope", 1},
        {Case::Port::File, "write_chunk", 1},
        {Case::Port::File, "write_chunk", 2},
        {Case::Port::File, "flush", 1},
        {Case::Port::File, "sync", 1},
        {Case::Port::File, "close", 1},
        {Case::Port::File, "read_envelope", 1},
        {Case::Port::File, "read_envelope", 2},
        {Case::Port::File, "read_package", 1},
        {Case::Port::File, "read_package", 2},
        {Case::Port::Allocator, "allocate", 1},
        {Case::Port::Allocator, "allocate", 2},
        {Case::Port::Selection, "nvs_stage", 1},
        {Case::Port::Selection, "nvs_commit", 1},
    }};

    for (const auto& item : cases) {
        Rig rig = installed;
        switch (item.port) {
        case Case::Port::File:
            rig.fs.failure = {item.operation, item.occurrence};
            break;
        case Case::Port::Allocator:
            rig.allocator.failure = {item.operation, item.occurrence};
            break;
        case Case::Port::Selection:
            rig.selections.failure = {item.operation, item.occurrence};
            break;
        }
        (void)install(rig, next_package);
        const bool triggered =
            item.port == Case::Port::File ? rig.fs.failure.triggered :
            item.port == Case::Port::Allocator ?
                rig.allocator.failure.triggered :
                rig.selections.failure.triggered;
        assert(triggered);
        const auto after_boot = reboot(rig);
        assert(after_boot.source == codex_pet::Source::Custom);
        assert(after_boot.generation == 4 || after_boot.generation == 5);
        assert(after_boot.hash == old_hash || after_boot.hash == next_hash);
        assert(after_boot.usable);
    }
}

void power_cycle(Rig& rig)
{
    rig.fs.open.fill(false);
    rig.fs.crash = {};
    rig.allocator.live = 0;
    rig.allocator.crash = {};
    rig.selections.pending.reset();
    rig.selections.crash_before = {};
    rig.selections.crash = {};
}

void test_power_loss_after_each_successful_boundary_never_selects_staging(
    const std::vector<uint8_t>& base)
{
    const auto old_package = package_variant(base, 181);
    const auto new_package = package_variant(base, 182);
    const auto old_hash = metadata(old_package).hash;
    const auto new_hash = metadata(new_package).hash;

    struct Case {
        enum class Port { File, Allocator, SelectionBefore, Selection } port;
        const char* operation;
        size_t occurrence;
        uint32_t expected_generation;
    };
    const std::array<Case, 18> cases = {{
        {Case::Port::File, "ensure_directory", 1, 1},
        {Case::Port::File, "remove", 1, 1},
        {Case::Port::File, "free_bytes", 1, 1},
        {Case::Port::File, "open_staging", 1, 1},
        {Case::Port::File, "write_envelope", 1, 1},
        {Case::Port::File, "write_chunk", 1, 1},
        {Case::Port::File, "write_chunk", 2, 1},
        {Case::Port::File, "flush", 1, 1},
        {Case::Port::File, "sync", 1, 1},
        {Case::Port::File, "close", 1, 1},
        {Case::Port::File, "file_size", 1, 1},
        {Case::Port::File, "read_envelope", 1, 1},
        {Case::Port::File, "read_package", 1, 1},
        {Case::Port::Allocator, "allocate", 1, 1},
        {Case::Port::SelectionBefore, "nvs_stage", 1, 1},
        {Case::Port::Selection, "nvs_stage", 1, 1},
        {Case::Port::Selection, "nvs_commit", 1, 2},
        {Case::Port::File, "remove", 2, 2},
    }};

    for (const auto& item : cases) {
        Rig rig{};
        assert(install(rig, old_package) == codex_pet::StoreResult::Ok);
        bool crashed = false;
        try {
            codex_pet::CodexPetStore store(
                rig.fs, rig.allocator, rig.selections);
            assert(store.load_selection().generation == 1);
            switch (item.port) {
            case Case::Port::File:
                rig.fs.crash = {item.operation, item.occurrence};
                break;
            case Case::Port::Allocator:
                rig.allocator.crash = {item.operation, item.occurrence};
                break;
            case Case::Port::SelectionBefore:
                rig.selections.crash_before = {
                    item.operation, item.occurrence};
                break;
            case Case::Port::Selection:
                rig.selections.crash = {item.operation, item.occurrence};
                break;
            }
            assert(
                store.begin(
                    static_cast<uint32_t>(new_package.size()),
                    metadata(new_package)) == codex_pet::StoreResult::Ok);
            const size_t split = new_package.size() / 2U;
            assert(
                store.write(0, {new_package.data(), split}) ==
                codex_pet::StoreResult::Ok);
            assert(
                store.write(
                    static_cast<uint32_t>(split),
                    {new_package.data() + split,
                     new_package.size() - split}) ==
                codex_pet::StoreResult::Ok);
            assert(store.commit() == codex_pet::StoreResult::Ok);
            assert(store.acknowledge() == codex_pet::StoreResult::Ok);
        } catch (const PowerLoss&) {
            crashed = true;
        }
        assert(crashed);
        const bool triggered =
            item.port == Case::Port::File ? rig.fs.crash.triggered :
            item.port == Case::Port::Allocator ?
                rig.allocator.crash.triggered :
            item.port == Case::Port::SelectionBefore ?
                rig.selections.crash_before.triggered :
                rig.selections.crash.triggered;
        assert(triggered);

        power_cycle(rig);
        const auto after_boot = reboot(rig);
        assert(after_boot.source == codex_pet::Source::Custom);
        assert(after_boot.generation == item.expected_generation);
        assert(
            after_boot.hash ==
            (item.expected_generation == 1 ? old_hash : new_hash));
        assert(after_boot.usable);
    }

    Rig load_crash{};
    assert(install(load_crash, old_package) == codex_pet::StoreResult::Ok);
    load_crash.selections.crash = {"nvs_load", 1};
    bool crashed = false;
    try {
        codex_pet::CodexPetStore store(
            load_crash.fs, load_crash.allocator, load_crash.selections);
        (void)store.load_selection();
    } catch (const PowerLoss&) {
        crashed = true;
    }
    assert(crashed && load_crash.selections.crash.triggered);
    power_cycle(load_crash);
    const auto after_boot = reboot(load_crash);
    assert(after_boot.source == codex_pet::Source::Custom);
    assert(after_boot.generation == 1);
    assert(after_boot.hash == old_hash);
    assert(after_boot.usable);
}

void test_acknowledgement_precedes_old_slot_cleanup(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto old_package = package_variant(base, 181);
    const auto new_package = package_variant(base, 182);
    assert(install(rig, old_package) == codex_pet::StoreResult::Ok);

    codex_pet::CodexPetStore store(rig.fs, rig.allocator, rig.selections);
    assert(
        store.begin(
            static_cast<uint32_t>(new_package.size()),
            metadata(new_package)) == codex_pet::StoreResult::Ok);
    assert(
        store.write(0, {new_package.data(), new_package.size()}) ==
        codex_pet::StoreResult::Ok);
    assert(store.commit() == codex_pet::StoreResult::Ok);
    const auto committed = store.load_selection();
    assert(committed.generation == 2);
    assert(rig.fs.present[0] && rig.fs.present[1]);
    assert(store.generation() == 2);
    assert(store.acknowledge() == codex_pet::StoreResult::Ok);
    assert(!rig.fs.present[0] && rig.fs.present[1]);

    Rig warning{};
    assert(install(warning, old_package) == codex_pet::StoreResult::Ok);
    codex_pet::CodexPetStore warning_store(
        warning.fs, warning.allocator, warning.selections);
    assert(
        warning_store.begin(
            static_cast<uint32_t>(new_package.size()),
            metadata(new_package)) == codex_pet::StoreResult::Ok);
    assert(
        warning_store.write(0, {new_package.data(), new_package.size()}) ==
        codex_pet::StoreResult::Ok);
    assert(warning_store.commit() == codex_pet::StoreResult::Ok);
    warning.fs.failure = {"remove", 1};
    assert(
        warning_store.acknowledge() ==
        codex_pet::StoreResult::CleanupWarning);
    assert(reboot(warning).generation == 2);
}

void test_reboot_preserves_previous_slot_until_explicit_acknowledgement(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto old_package = package_variant(base, 181);
    const auto new_package = package_variant(base, 182);
    const auto later_package = package_variant(base, 183);
    assert(install(rig, old_package) == codex_pet::StoreResult::Ok);
    assert(
        install(rig, new_package, false) ==
        codex_pet::StoreResult::Ok);
    assert(rig.selections.persistent.has_value());
    const uint8_t new_slot = rig.selections.persistent->encoded[9];
    const uint8_t previous_slot = static_cast<uint8_t>(new_slot ^ 1U);
    assert(rig.fs.present[new_slot] && rig.fs.present[previous_slot]);

    codex_pet::CodexPetStore rebooted(
        rig.fs, rig.allocator, rig.selections);
    const auto selected = rebooted.load_selection();
    assert(selected.source == codex_pet::Source::Custom);
    assert(selected.generation == 2);
    assert(rig.fs.present[new_slot] && rig.fs.present[previous_slot]);
    assert(
        rebooted.begin(
            static_cast<uint32_t>(later_package.size()),
            metadata(later_package)) == codex_pet::StoreResult::Busy);
    assert(rig.fs.present[new_slot] && rig.fs.present[previous_slot]);
    assert(rebooted.acknowledge() == codex_pet::StoreResult::Ok);
    assert(rig.fs.present[new_slot] && !rig.fs.present[previous_slot]);
}

void test_failed_selection_repair_still_requires_cleanup_acknowledgement(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto old_package = package_variant(base, 181);
    const auto new_package = package_variant(base, 182);
    const auto later_package = package_variant(base, 183);
    assert(install(rig, old_package) == codex_pet::StoreResult::Ok);
    assert(
        install(rig, new_package, false) ==
        codex_pet::StoreResult::Ok);
    assert(rig.selections.persistent.has_value());
    rig.selections.persistent->encoded[48] ^= 0x80U;
    rig.selections.failure = {"nvs_stage", 1};

    codex_pet::CodexPetStore rebooted(
        rig.fs, rig.allocator, rig.selections);
    const auto selected = rebooted.load_selection();
    assert(selected.source == codex_pet::Source::Custom);
    assert(selected.generation == 2);
    assert(rig.selections.failure.triggered);
    assert(rig.fs.present[0] && rig.fs.present[1]);
    assert(
        rebooted.begin(
            static_cast<uint32_t>(later_package.size()),
            metadata(later_package)) == codex_pet::StoreResult::Busy);
    assert(rig.fs.present[0] && rig.fs.present[1]);
}

void test_capacity_gate_and_active_slot_preservation(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto old_package = package_variant(base, 181);
    const auto new_package = package_variant(base, 182);
    assert(install(rig, old_package) == codex_pet::StoreResult::Ok);
    const uint64_t required =
        new_package.size() + codex_pet::kSlotEnvelopeBytes +
        codex_pet::kStorageReserveBytes;

    rig.fs.removed_slots.clear();
    rig.fs.available = required - 1U;
    codex_pet::CodexPetStore rejected(rig.fs, rig.allocator, rig.selections);
    assert(
        rejected.begin(
            static_cast<uint32_t>(new_package.size()),
            metadata(new_package)) ==
        codex_pet::StoreResult::InsufficientStorage);
    assert(rejected.required_bytes() == required);
    assert(rejected.available_bytes() == required - 1U);
    assert(rig.fs.present[0]);
    assert(std::find(
               rig.fs.removed_slots.begin(),
               rig.fs.removed_slots.end(),
               uint8_t{0}) == rig.fs.removed_slots.end());

    rig.fs.available = required;
    codex_pet::CodexPetStore exact(rig.fs, rig.allocator, rig.selections);
    assert(
        exact.begin(
            static_cast<uint32_t>(new_package.size()),
            metadata(new_package)) == codex_pet::StoreResult::Ok);
    exact.cancel();

    rig.fs.available = std::numeric_limits<uint64_t>::max();
    codex_pet::CodexPetStore huge(rig.fs, rig.allocator, rig.selections);
    assert(
        huge.begin(
            static_cast<uint32_t>(new_package.size()),
            metadata(new_package)) == codex_pet::StoreResult::Ok);
    huge.cancel();
}

void test_chunk_duplicate_gap_and_conflict_retire_staging(
    const std::vector<uint8_t>& base)
{
    const auto package = package_variant(base, 181);
    const size_t half = package.size() / 2U;

    Rig exact{};
    codex_pet::CodexPetStore store(
        exact.fs, exact.allocator, exact.selections);
    assert(
        store.begin(
            static_cast<uint32_t>(package.size()), metadata(package)) ==
        codex_pet::StoreResult::Ok);
    assert(
        store.write(0, {package.data(), half}) ==
        codex_pet::StoreResult::Ok);
    assert(
        store.write(0, {package.data(), half}) ==
        codex_pet::StoreResult::Ok);
    auto conflicting = package;
    conflicting[0] ^= 0x01U;
    assert(
        store.write(0, {conflicting.data(), half}) ==
        codex_pet::StoreResult::InvalidOffset);
    assert(reboot(exact).source == codex_pet::Source::BuiltIn);

    Rig gap{};
    codex_pet::CodexPetStore gap_store(
        gap.fs, gap.allocator, gap.selections);
    assert(
        gap_store.begin(
            static_cast<uint32_t>(package.size()), metadata(package)) ==
        codex_pet::StoreResult::Ok);
    assert(
        gap_store.write(1, {package.data(), half}) ==
        codex_pet::StoreResult::InvalidOffset);
    assert(reboot(gap).source == codex_pet::Source::BuiltIn);
}

void test_complete_parse_digest_and_metadata_validation(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto old_package = package_variant(base, 181);
    assert(install(rig, old_package) == codex_pet::StoreResult::Ok);

    auto digest_mismatch = package_variant(base, 182);
    const auto digest_metadata = metadata(digest_mismatch);
    digest_mismatch.back() ^= 0x01U;
    codex_pet::CodexPetStore digest_store(
        rig.fs, rig.allocator, rig.selections);
    assert(
        digest_store.begin(
            static_cast<uint32_t>(digest_mismatch.size()), digest_metadata) ==
        codex_pet::StoreResult::Ok);
    assert(
        digest_store.write(
            0, {digest_mismatch.data(), digest_mismatch.size()}) ==
        codex_pet::StoreResult::Ok);
    assert(
        digest_store.commit() == codex_pet::StoreResult::DigestMismatch);
    assert(reboot(rig).generation == 1);

    auto malformed = package_variant(base, 183);
    malformed.back() = 0xffU;
    redigest(malformed);
    codex_pet::CodexPetStore parse_store(
        rig.fs, rig.allocator, rig.selections);
    assert(
        parse_store.begin(
            static_cast<uint32_t>(malformed.size()), metadata(malformed)) ==
        codex_pet::StoreResult::Ok);
    assert(
        parse_store.write(0, {malformed.data(), malformed.size()}) ==
        codex_pet::StoreResult::Ok);
    assert(
        parse_store.commit() == codex_pet::StoreResult::InvalidPackage);
    assert(reboot(rig).generation == 1);

    auto wrong = metadata(old_package);
    wrong.width = 143;
    codex_pet::CodexPetStore metadata_store(
        rig.fs, rig.allocator, rig.selections);
    assert(
        metadata_store.begin(
            static_cast<uint32_t>(old_package.size()), wrong) ==
        codex_pet::StoreResult::InvalidMetadata);
}

void test_commit_rejects_metadata_that_disagrees_with_complete_cxp(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto old_package = package_variant(base, 181);
    const auto new_package = package_variant(base, 182);
    assert(install(rig, old_package) == codex_pet::StoreResult::Ok);
    auto declared = metadata(new_package);
    assert(declared.frame_count < codex_pet::kMaxFrames);
    ++declared.frame_count;

    codex_pet::CodexPetStore store(
        rig.fs, rig.allocator, rig.selections);
    assert(
        store.begin(
            static_cast<uint32_t>(new_package.size()), declared) ==
        codex_pet::StoreResult::Ok);
    assert(
        store.write(0, {new_package.data(), new_package.size()}) ==
        codex_pet::StoreResult::Ok);
    assert(store.commit() == codex_pet::StoreResult::DigestMismatch);
    const auto selected = reboot(rig);
    assert(selected.source == codex_pet::Source::Custom);
    assert(selected.generation == 1);
    assert(selected.hash == metadata(old_package).hash);
}

void test_boot_scans_both_slots_repairs_crc_and_rejects_truncation(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto generation_one = package_variant(base, 181);
    const auto generation_two = package_variant(base, 182);
    assert(install(rig, generation_one) == codex_pet::StoreResult::Ok);
    assert(install(rig, generation_two, false) == codex_pet::StoreResult::Ok);
    assert(rig.fs.present[0] && rig.fs.present[1]);
    assert(rig.selections.persistent.has_value());
    rig.selections.persistent->encoded[0] ^= 0x01U;

    const auto recovered = reboot(rig);
    assert(recovered.source == codex_pet::Source::Custom);
    assert(recovered.generation == 2);
    assert(recovered.hash == metadata(generation_two).hash);
    assert(rig.selections.persistent->encoded[0] == 'C');

    Rig truncated{};
    assert(install(truncated, generation_one) == codex_pet::StoreResult::Ok);
    assert(
        install(truncated, generation_two, false) ==
        codex_pet::StoreResult::Ok);
    const uint8_t active = 1;
    truncated.fs.files[active].resize(codex_pet::kSlotEnvelopeBytes + 3U);
    truncated.selections.persistent->encoded[48] ^= 0x80U;
    const auto old_recovered = reboot(truncated);
    assert(old_recovered.source == codex_pet::Source::Custom);
    assert(old_recovered.generation == 1);
    assert(old_recovered.hash == metadata(generation_one).hash);
    assert(!truncated.fs.present[active]);

    truncated.fs.files[old_recovered.slot].resize(12);
    truncated.selections.persistent->encoded[48] ^= 0x40U;
    const auto built_in = reboot(truncated);
    assert(built_in.source == codex_pet::Source::BuiltIn);
    assert(built_in.generation == 0);
    assert(built_in.usable);
}

void test_envelope_crc_corruption_never_wins_recovery(
    const std::vector<uint8_t>& base)
{
    const auto generation_one = package_variant(base, 181);
    const auto generation_two = package_variant(base, 182);

    Rig recover_old{};
    assert(
        install(recover_old, generation_one) ==
        codex_pet::StoreResult::Ok);
    assert(
        install(recover_old, generation_two, false) ==
        codex_pet::StoreResult::Ok);
    assert(recover_old.selections.persistent.has_value());
    const uint8_t new_slot = recover_old.selections.persistent->encoded[9];
    const uint8_t old_slot = static_cast<uint8_t>(new_slot ^ 1U);
    recover_old.fs.files[new_slot][52] ^= 0x01U;
    recover_old.selections.persistent->encoded[48] ^= 0x80U;

    const auto recovered = reboot(recover_old);
    assert(recovered.source == codex_pet::Source::Custom);
    assert(recovered.generation == 1);
    assert(recovered.hash == metadata(generation_one).hash);
    assert(recovered.slot == old_slot);
    assert(recover_old.fs.present[old_slot]);
    assert(!recover_old.fs.present[new_slot]);

    Rig no_valid_slot{};
    assert(
        install(no_valid_slot, generation_one) ==
        codex_pet::StoreResult::Ok);
    assert(no_valid_slot.selections.persistent.has_value());
    const uint8_t only_slot = no_valid_slot.selections.persistent->encoded[9];
    no_valid_slot.fs.files[only_slot][52] ^= 0x01U;

    const auto built_in = reboot(no_valid_slot);
    assert(built_in.source == codex_pet::Source::BuiltIn);
    assert(built_in.generation == 0);
    assert(built_in.usable);
    assert(!no_valid_slot.fs.present[only_slot]);
}

void rewrite_as_unsupported_schema(Rig& rig, uint8_t slot)
{
    auto& file = rig.fs.files[slot];
    assert(file.size() > codex_pet::kSlotEnvelopeBytes);
    auto package = std::vector<uint8_t>(
        file.begin() + codex_pet::kSlotEnvelopeBytes,
        file.end());
    write_u16(package.data() + 4, 2);
    redigest(package);
    std::copy(
        package.begin(),
        package.end(),
        file.begin() + codex_pet::kSlotEnvelopeBytes);
    write_u16(file.data() + 16, 2);
    std::copy_n(
        package.data() + kDigestOffset,
        kDigestSize,
        file.begin() + 20);
    write_u32(file.data() + 52, 0);
    write_u32(
        file.data() + 52,
        crc32(file.data(), codex_pet::kSlotEnvelopeBytes));

    auto& record = rig.selections.persistent->encoded;
    write_u16(record.data() + 10, 2);
    std::copy_n(
        package.data() + kDigestOffset,
        kDigestSize,
        record.begin() + 16);
    write_u32(record.data() + 48, 0);
    write_u32(record.data() + 48, crc32(record.data(), record.size()));
}

void test_unsupported_schema_falls_back_without_deletion(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto package = package_variant(base, 181);
    assert(install(rig, package) == codex_pet::StoreResult::Ok);
    const auto installed = reboot(rig);
    rewrite_as_unsupported_schema(rig, installed.slot);

    const auto fallback = reboot(rig);
    assert(fallback.source == codex_pet::Source::BuiltIn);
    assert(fallback.generation == 1);
    assert(!fallback.usable);
    assert(rig.fs.present[installed.slot]);
    assert(read_u16(
               rig.fs.files[installed.slot].data() + 16) == 2);
}

void test_recovery_ranks_generation_before_schema_support(
    const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto generation_one = package_variant(base, 181);
    const auto generation_two = package_variant(base, 182);
    assert(install(rig, generation_one) == codex_pet::StoreResult::Ok);
    assert(
        install(rig, generation_two, false) ==
        codex_pet::StoreResult::Ok);
    assert(rig.selections.persistent.has_value());
    const uint8_t highest_slot = rig.selections.persistent->encoded[9];
    rewrite_as_unsupported_schema(rig, highest_slot);
    rig.selections.persistent->encoded[48] ^= 0x80U;

    const auto fallback = reboot(rig);
    assert(fallback.source == codex_pet::Source::BuiltIn);
    assert(fallback.generation == 2);
    assert(!fallback.usable);
    assert(rig.fs.present[highest_slot]);
    assert(rig.selections.persistent.has_value());
    assert(
        rig.selections.persistent->encoded[8] ==
        static_cast<uint8_t>(codex_pet::Source::Custom));
    assert(rig.selections.persistent->encoded[9] == highest_slot);
    assert(read_u16(rig.selections.persistent->encoded.data() + 10) == 2);
    assert(read_u32(rig.selections.persistent->encoded.data() + 12) == 2);
}

void test_restore_default_commit_and_cleanup_faults(
    const std::vector<uint8_t>& base)
{
    const auto package = package_variant(base, 181);
    Rig warning{};
    assert(install(warning, package) == codex_pet::StoreResult::Ok);
    codex_pet::CodexPetStore store(
        warning.fs, warning.allocator, warning.selections);
    assert(store.load_selection().source == codex_pet::Source::Custom);
    assert(store.restore_default() == codex_pet::StoreResult::Ok);
    assert(warning.fs.present[0]);
    warning.fs.failure = {"remove", 1};
    assert(store.acknowledge() == codex_pet::StoreResult::CleanupWarning);
    const auto selected = reboot(warning);
    assert(selected.source == codex_pet::Source::BuiltIn);
    assert(selected.generation == 2);
    assert(selected.usable);

    for (const char* fault : {"nvs_stage", "nvs_commit"}) {
        Rig failed{};
        assert(install(failed, package) == codex_pet::StoreResult::Ok);
        codex_pet::CodexPetStore failed_store(
            failed.fs, failed.allocator, failed.selections);
        assert(failed_store.load_selection().source == codex_pet::Source::Custom);
        failed.selections.failure = {fault, 1};
        assert(
            failed_store.restore_default() ==
            codex_pet::StoreResult::SelectionIoError);
        const auto after_boot = reboot(failed);
        assert(after_boot.source == codex_pet::Source::Custom);
        assert(after_boot.generation == 1);
    }
}

void test_generation_wrap_is_rejected(const std::vector<uint8_t>& base)
{
    Rig rig{};
    const auto package = package_variant(base, 181);
    assert(install(rig, package) == codex_pet::StoreResult::Ok);
    const auto active = reboot(rig);

    auto& file = rig.fs.files[active.slot];
    write_u32(file.data() + 8, std::numeric_limits<uint32_t>::max());
    write_u32(file.data() + 52, 0);
    write_u32(
        file.data() + 52,
        crc32(file.data(), codex_pet::kSlotEnvelopeBytes));
    auto& record = rig.selections.persistent->encoded;
    write_u32(record.data() + 12, std::numeric_limits<uint32_t>::max());
    write_u32(record.data() + 48, 0);
    write_u32(record.data() + 48, crc32(record.data(), record.size()));

    codex_pet::CodexPetStore store(rig.fs, rig.allocator, rig.selections);
    assert(
        store.begin(
            static_cast<uint32_t>(package.size()), metadata(package)) ==
        codex_pet::StoreResult::GenerationExhausted);
    assert(
        store.restore_default() ==
        codex_pet::StoreResult::GenerationExhausted);
}

void test_production_contract_constants_and_psram_sized_validation(
    const std::vector<uint8_t>& base)
{
    assert(std::strcmp(codex_pet::kPetDirectory, "/spiflash/codex_pet") == 0);
    assert(std::strcmp(
               codex_pet::kPetSlotPaths[0],
               "/spiflash/codex_pet/slot0.cxp") == 0);
    assert(std::strcmp(
               codex_pet::kPetSlotPaths[1],
               "/spiflash/codex_pet/slot1.cxp") == 0);
    assert(std::strcmp(codex_pet::kPetNvsNamespace, "cxpet") == 0);
    assert(std::strcmp(codex_pet::kPetNvsKey, "active") == 0);
    assert(std::strlen(codex_pet::kPetNvsNamespace) <= 15);
    assert(std::strlen(codex_pet::kPetNvsKey) <= 15);
    assert(codex_pet::kSlotEnvelopeBytes == 56);
    assert(codex_pet::kStorageReserveBytes == 131072);

    Rig rig{};
    const auto package = package_variant(base, 181);
    assert(install(rig, package) == codex_pet::StoreResult::Ok);
    const auto selected = reboot(rig);
    const auto& envelope_file = rig.fs.files[selected.slot];
    assert(envelope_file.size() == package.size() + codex_pet::kSlotEnvelopeBytes);
    assert(std::equal(
        envelope_file.begin(), envelope_file.begin() + 4, "CXS1"));
    assert(read_u16(envelope_file.data() + 4) == 1);
    assert(read_u16(envelope_file.data() + 6) == 56);
    assert(read_u32(envelope_file.data() + 8) == 1);
    assert(read_u32(envelope_file.data() + 12) == package.size());
    assert(read_u16(envelope_file.data() + 16) == 1);
    assert(read_u16(envelope_file.data() + 18) == 0);
    assert(std::equal(
        selected.hash.begin(), selected.hash.end(), envelope_file.begin() + 20));
    std::array<uint8_t, codex_pet::kSlotEnvelopeBytes> envelope{};
    std::copy_n(envelope_file.begin(), envelope.size(), envelope.begin());
    const uint32_t envelope_crc = read_u32(envelope.data() + 52);
    write_u32(envelope.data() + 52, 0);
    assert(envelope_crc == crc32(envelope.data(), envelope.size()));

    assert(rig.selections.persistent.has_value());
    auto record = rig.selections.persistent->encoded;
    assert(std::equal(record.begin(), record.begin() + 4, "CXR1"));
    assert(read_u16(record.data() + 4) == 1);
    assert(read_u16(record.data() + 6) == record.size());
    assert(record[8] == static_cast<uint8_t>(codex_pet::Source::Custom));
    assert(record[9] == selected.slot);
    assert(read_u16(record.data() + 10) == 1);
    assert(read_u32(record.data() + 12) == 1);
    assert(std::equal(selected.hash.begin(), selected.hash.end(), record.begin() + 16));
    const uint32_t record_crc = read_u32(record.data() + 48);
    write_u32(record.data() + 48, 0);
    assert(record_crc == crc32(record.data(), record.size()));

    assert(!rig.allocator.requests.empty());
    assert(std::all_of(
        rig.allocator.requests.begin(),
        rig.allocator.requests.end(),
        [&](size_t request) { return request == package.size(); }));
    assert(rig.allocator.live == 0);
    assert(rig.allocator.maximum_live == 1);
}

}  // namespace

int main()
{
    const auto base = read_hex("tests/fixtures/codex_pet/cxp1_minimal.hex");
    test_commit_fault_matrix_keeps_only_complete_generations(base);
    test_power_loss_after_each_successful_boundary_never_selects_staging(base);
    test_acknowledgement_precedes_old_slot_cleanup(base);
    test_reboot_preserves_previous_slot_until_explicit_acknowledgement(base);
    test_failed_selection_repair_still_requires_cleanup_acknowledgement(base);
    test_capacity_gate_and_active_slot_preservation(base);
    test_chunk_duplicate_gap_and_conflict_retire_staging(base);
    test_complete_parse_digest_and_metadata_validation(base);
    test_commit_rejects_metadata_that_disagrees_with_complete_cxp(base);
    test_boot_scans_both_slots_repairs_crc_and_rejects_truncation(base);
    test_envelope_crc_corruption_never_wins_recovery(base);
    test_unsupported_schema_falls_back_without_deletion(base);
    test_recovery_ranks_generation_before_schema_support(base);
    test_restore_default_commit_and_cleanup_faults(base);
    test_generation_wrap_is_rejected(base);
    test_production_contract_constants_and_psram_sized_validation(base);
    return 0;
}
