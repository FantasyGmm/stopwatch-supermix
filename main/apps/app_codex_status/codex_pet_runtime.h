#pragma once

#include "codex_pet_store.h"

#include <cstddef>
#include <cstdint>

#if defined(STOPWATCH_HOST_TEST)
typedef uint32_t lv_color_format_t;

struct lv_image_header_t {
    uint32_t magic : 8;
    uint32_t cf : 8;
    uint32_t flags : 16;
    uint32_t w : 16;
    uint32_t h : 16;
    uint32_t stride : 16;
    uint32_t reserved_2 : 16;
};

struct lv_image_dsc_t {
    lv_image_header_t header{};
    uint32_t data_size = 0;
    const uint8_t* data = nullptr;
    const void* reserved = nullptr;
    const void* reserved_2 = nullptr;
};

constexpr uint32_t LV_IMAGE_HEADER_MAGIC = 0x19;
constexpr lv_color_format_t LV_COLOR_FORMAT_RGB565A8 = 12;
#else
#include <lvgl.h>
#endif

namespace codex_pet {

enum class PetRuntimeDiagnostic : uint8_t {
    None,
    InvalidPackage,
    AllocationFailure,
    DecodeFailure,
};

class PetRuntimeDiagnosticSink {
public:
    virtual ~PetRuntimeDiagnosticSink() = default;
    virtual void record(PetRuntimeDiagnostic diagnostic) = 0;
};

class PetFrameDecoder {
public:
    virtual ~PetFrameDecoder() = default;
    virtual DecodeError decode(
        const PackageView& package,
        uint16_t frame_index,
        MutableByteView output) = 0;
};

class CodexPetRuntime {
public:
    CodexPetRuntime(
        PetFileSystem& files,
        PetBufferAllocator& allocator,
        PetFrameDecoder* decoder = nullptr,
        PetRuntimeDiagnosticSink* diagnostics = nullptr);
    ~CodexPetRuntime();

    CodexPetRuntime(const CodexPetRuntime&) = delete;
    CodexPetRuntime& operator=(const CodexPetRuntime&) = delete;

    bool open(const PetSelection& selection);
    bool prepare_generation(const PetSelection& selection);
    void set_visible(bool visible, uint32_t now_ms);
    void advance(uint32_t now_ms);
    const lv_image_dsc_t* current_image() const;
    bool take_frame_changed();
    void acknowledge_image_published();
    bool using_custom() const;
    uint32_t next_frame_delay_ms(uint32_t now_ms) const;
    void close();

#if defined(STOPWATCH_HOST_TEST)
    uint32_t frame_deadline_ms_for_test() const;
#endif

private:
    struct Candidate {
        uint8_t* package_bytes = nullptr;
        uint8_t* frame_bytes = nullptr;
        PackageView package{};
        lv_image_dsc_t image{};
    };

    bool load_candidate(const PetSelection& selection, Candidate& candidate);
    bool decode_into_current(uint16_t frame_index);
    bool decode_candidate(Candidate& candidate, uint16_t frame_index);
    DecodeError decode(
        const PackageView& package,
        uint16_t frame_index,
        MutableByteView output);
    void activate_candidate(Candidate& candidate);
    void activate_builtin();
    void swap_pending();
    bool advance_current_frame();
    void coalesce_completed_cycles(uint32_t now_ms);
    void release_candidate(Candidate& candidate);
    void release_current_custom();
    void retire_current_custom();
    void release_retired();
    void release_pending();
    void fallback(PetRuntimeDiagnostic diagnostic);
    void report_once(PetRuntimeDiagnostic diagnostic);
    uint32_t displayed_frame_duration_ms() const;
    uint32_t animation_cycle_duration_ms() const;
    bool has_retired_image() const;
    static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms);

    PetFileSystem& _files;
    PetBufferAllocator& _allocator;
    PetFrameDecoder* _decoder = nullptr;
    PetRuntimeDiagnosticSink* _diagnostics = nullptr;
    Candidate _current{};
    Candidate _retired{};
    Candidate _pending{};
    const lv_image_dsc_t* _current_image = nullptr;
    uint16_t _frame_index = 0;
    uint32_t _frame_deadline_ms = 0;
    bool _frame_deadline_armed = false;
    bool _visible = false;
    bool _using_custom = false;
    bool _pending_builtin = false;
    bool _frame_changed = false;
    bool _diagnostic_reported = false;
};

}  // namespace codex_pet
