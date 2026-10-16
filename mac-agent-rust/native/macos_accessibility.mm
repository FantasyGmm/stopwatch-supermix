#include "macos_accessibility.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr CFIndex kMaxComposerNodes = 64;
constexpr CFIndex kMaxComposerDepth = 4;
constexpr int32_t kUnknown = 0;
constexpr int32_t kSend = 1;
constexpr int32_t kQueue = 2;
constexpr int32_t kSteer = 3;
constexpr int32_t kRunning = 4;
constexpr CGKeyCode kReturnKeyCode = 0x24;
constexpr CGEventFlags kAlternateFlags = kCGEventFlagMaskCommand;

const CFStringRef kChromeNodeId = CFSTR("ChromeAXNodeId");
const CFStringRef kDomClassList = CFSTR("AXDOMClassList");

struct PreparedTarget {
    pid_t pid = 0;
    AXUIElementRef focused = nullptr;
    AXUIElementRef button = nullptr;
    CFTypeRef focused_token = nullptr;
    CFTypeRef button_token = nullptr;
    CFStringRef value = nullptr;
    CFStringRef original_value = nullptr;
    int32_t semantic = kUnknown;

    ~PreparedTarget() {
        if (focused) CFRelease(focused);
        if (button) CFRelease(button);
        if (focused_token) CFRelease(focused_token);
        if (button_token) CFRelease(button_token);
        if (value) CFRelease(value);
        if (original_value) CFRelease(original_value);
    }
};

struct Inspection {
    SwaxSnapshot snapshot{};
    std::unique_ptr<PreparedTarget> target;
};

std::mutex g_targets_mutex;
std::unordered_map<uint64_t, std::unique_ptr<PreparedTarget>> g_targets;
std::atomic<uint64_t> g_next_handle{1};

CFTypeRef copy_attribute(AXUIElementRef element, CFStringRef attribute) {
    if (!element) return nullptr;
    CFTypeRef value = nullptr;
    if (AXUIElementCopyAttributeValue(element, attribute, &value) != kAXErrorSuccess) {
        if (value) CFRelease(value);
        return nullptr;
    }
    return value;
}

bool is_true(CFTypeRef value) {
    return value && CFGetTypeID(value) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(static_cast<CFBooleanRef>(value));
}

bool is_enabled(AXUIElementRef element) {
    CFTypeRef value = copy_attribute(element, kAXEnabledAttribute);
    const bool enabled = value && CFGetTypeID(value) == CFBooleanGetTypeID() &&
        CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    if (value) CFRelease(value);
    return enabled;
}

bool is_hidden(AXUIElementRef element) {
    CFTypeRef value = copy_attribute(element, kAXHiddenAttribute);
    const bool hidden = is_true(value);
    if (value) CFRelease(value);
    return hidden;
}

bool is_explicitly_visible(AXUIElementRef element) {
    CFTypeRef value = copy_attribute(element, kAXHiddenAttribute);
    const bool visible = value && CFGetTypeID(value) == CFBooleanGetTypeID() &&
        !CFBooleanGetValue(static_cast<CFBooleanRef>(value));
    if (value) CFRelease(value);
    return visible;
}

bool visible_within_root(AXUIElementRef element, AXUIElementRef root) {
    if (!element || !root) return false;
    AXUIElementRef current = static_cast<AXUIElementRef>(CFRetain(element));
    for (CFIndex depth = 0; depth <= kMaxComposerDepth; ++depth) {
        if (!is_explicitly_visible(current)) { CFRelease(current); return false; }
        if (CFEqual(current, root)) { CFRelease(current); return true; }
        CFTypeRef raw_parent = copy_attribute(current, kAXParentAttribute);
        CFRelease(current);
        if (!raw_parent || CFGetTypeID(raw_parent) != AXUIElementGetTypeID()) {
            if (raw_parent) CFRelease(raw_parent);
            return false;
        }
        current = static_cast<AXUIElementRef>(raw_parent);
    }
    CFRelease(current);
    return false;
}

bool is_string(CFTypeRef value) {
    return value && CFGetTypeID(value) == CFStringGetTypeID();
}

NSString *ns_string(CFStringRef value) {
    return (__bridge NSString *)value;
}

bool string_equals(CFTypeRef value, CFStringRef expected) {
    return is_string(value) && CFStringCompare(static_cast<CFStringRef>(value), expected, 0) == kCFCompareEqualTo;
}

std::vector<NSString *> string_values(CFTypeRef value) {
    std::vector<NSString *> values;
    if (is_string(value)) {
        values.push_back(ns_string(static_cast<CFStringRef>(value)));
        return values;
    }
    if (!value || CFGetTypeID(value) != CFArrayGetTypeID()) return values;
    CFArrayRef array = static_cast<CFArrayRef>(value);
    const CFIndex count = CFArrayGetCount(array);
    values.reserve(static_cast<size_t>(std::min(count, kMaxComposerNodes)));
    for (CFIndex i = 0; i < count && i < kMaxComposerNodes; ++i) {
        CFTypeRef item = CFArrayGetValueAtIndex(array, i);
        if (is_string(item)) values.push_back(ns_string(static_cast<CFStringRef>(item)));
    }
    return values;
}

bool has_class(AXUIElementRef element, NSString *marker) {
    CFTypeRef raw = copy_attribute(element, kDomClassList);
    if (!raw) return false;
    bool found = false;
    for (NSString *token : string_values(raw)) {
        NSArray<NSString *> *classes = [token componentsSeparatedByCharactersInSet:
            [NSCharacterSet whitespaceAndNewlineCharacterSet]];
        for (NSString *class_name in classes) {
            if ([class_name isEqualToString:marker]) {
                found = true;
                break;
            }
        }
        if (found) break;
    }
    CFRelease(raw);
    return found;
}

bool class_contains(AXUIElementRef element, NSString *exact) {
    CFTypeRef raw = copy_attribute(element, kDomClassList);
    if (!raw) return false;
    bool found = false;
    for (NSString *token : string_values(raw)) {
        if ([[token componentsSeparatedByCharactersInSet:
              [NSCharacterSet whitespaceAndNewlineCharacterSet]] containsObject:exact]) {
            found = true;
            break;
        }
        if ([token isEqualToString:exact]) {
            found = true;
            break;
        }
    }
    CFRelease(raw);
    return found;
}

struct WalkResult {
    std::vector<AXUIElementRef> elements;
    bool bounded = false;

    WalkResult() = default;
    WalkResult(const WalkResult&) = delete;
    WalkResult& operator=(const WalkResult&) = delete;
    WalkResult(WalkResult&& other) noexcept
        : elements(std::move(other.elements)), bounded(other.bounded) {
        other.elements.clear();
    }
    WalkResult& operator=(WalkResult&& other) noexcept {
        if (this == &other) return *this;
        for (AXUIElementRef element : elements) CFRelease(element);
        elements = std::move(other.elements);
        bounded = other.bounded;
        other.elements.clear();
        return *this;
    }
    ~WalkResult() {
        for (AXUIElementRef element : elements) CFRelease(element);
    }
};

WalkResult descendants(AXUIElementRef root, CFIndex max_nodes = kMaxComposerNodes,
                       CFIndex max_depth = kMaxComposerDepth) {
    WalkResult result;
    if (!root) return result;
    std::deque<std::pair<AXUIElementRef, CFIndex>> pending;
    pending.emplace_back(static_cast<AXUIElementRef>(CFRetain(root)), 0);
    while (!pending.empty()) {
        auto [element, depth] = pending.front();
        pending.pop_front();
        bool seen = false;
        for (AXUIElementRef previous : result.elements) {
            if (CFEqual(previous, element)) { seen = true; break; }
        }
        if (seen) { CFRelease(element); continue; }
        if (static_cast<CFIndex>(result.elements.size()) >= max_nodes) {
            result.bounded = true;
            CFRelease(element);
            for (const auto& entry : pending) CFRelease(entry.first);
            return result;
        }
        result.elements.push_back(element);
        CFTypeRef raw_children = copy_attribute(element, kAXChildrenAttribute);
        if (!raw_children) continue;
        if (CFGetTypeID(raw_children) == CFArrayGetTypeID()) {
            CFArrayRef children = static_cast<CFArrayRef>(raw_children);
            const CFIndex count = CFArrayGetCount(children);
            if (depth >= max_depth && count > 0) {
                result.bounded = true;
            }
            const size_t remaining = static_cast<size_t>(max_nodes) -
                std::min(static_cast<size_t>(max_nodes), result.elements.size() + pending.size());
            if (depth < max_depth && static_cast<size_t>(count) > remaining) {
                result.bounded = true;
            }
            for (CFIndex i = 0; i < count && depth < max_depth && !result.bounded; ++i) {
                CFTypeRef child = CFArrayGetValueAtIndex(children, i);
                if (child && CFGetTypeID(child) == AXUIElementGetTypeID()) {
                    pending.emplace_back(static_cast<AXUIElementRef>(CFRetain(child)), depth + 1);
                }
            }
        }
        CFRelease(raw_children);
        if (result.bounded) {
            for (const auto& entry : pending) CFRelease(entry.first);
            return result;
        }
    }
    return result;
}

bool recognized_composer_description(CFTypeRef description) {
    static const char *allowed[] = {
        "Message ChatGPT", "给 ChatGPT 发消息", "Do anything", "随心输入",
        "Follow up on this cloud chat", "跟进此云端聊天",
        "Create a new local task that references this cloud task",
        "创建引用此云端任务的新的本地任务",
        "Create a new cloud task that includes your current code and references this task",
        "创建包含当前代码并将引用此任务的新的云端任务",
        "Describe your goal, define measurable outcomes for best results",
        "描述你的目标，定义可衡量的成果，以获得最佳效果",
        "Follow up in a new cloud chat", "在新的云端聊天中跟进",
        "Ask for follow-up changes", "提出后续变更要求",
        "Ask for follow-up changes or @ to tag an agent",
        "要求提供后续变更或 @ 提及智能体",
        "Ask Codex to do anything in the cloud", "在云端向 Codex 下达任意指令​",
        "Ask Codex to do anything", "向 Codex 下达任意指令​",
        "Ask ChatGPT anything. @ to use plugins or mention files",
        "可向 ChatGPT 询问任何问题。输入 @ 使用插件或提及文件",
        "Ask Codex anything. @ to use plugins or mention files",
        "可向 Codex 询问任何问题。输入 @ 使用插件或提及文件",
        "Describe your task to generate a plan...", "描述你的任务以生成套餐…",
        "Work with ChatGPT", "使用 ChatGPT Work", "Working…", "处理中…"
    };
    if (!is_string(description)) return false;
    for (const char *candidate : allowed) {
        CFStringRef expected = CFStringCreateWithCString(kCFAllocatorDefault, candidate, kCFStringEncodingUTF8);
        if (!expected) continue;
        const bool match = CFStringCompare(static_cast<CFStringRef>(description), expected, 0) == kCFCompareEqualTo;
        CFRelease(expected);
        if (match) return true;
    }
    return false;
}

bool is_composer(AXUIElementRef element) {
    if (!element || is_hidden(element)) return false;
    CFTypeRef role = copy_attribute(element, kAXRoleAttribute);
    const bool correct_role = string_equals(role, kAXTextAreaRole);
    if (role) CFRelease(role);
    if (!correct_role || !is_enabled(element)) return false;
    Boolean settable = false;
    if (AXUIElementIsAttributeSettable(element, kAXValueAttribute, &settable) != kAXErrorSuccess || !settable) return false;
    CFTypeRef description = copy_attribute(element, kAXDescriptionAttribute);
    const bool supported_description = recognized_composer_description(description);
    if (description) CFRelease(description);
    if (!supported_description || !class_contains(element, @"ProseMirror")) return false;

    CFTypeRef raw_parent = copy_attribute(element, kAXParentAttribute);
    if (!raw_parent || CFGetTypeID(raw_parent) != AXUIElementGetTypeID()) {
        if (raw_parent) CFRelease(raw_parent);
        return false;
    }
    AXUIElementRef parent = static_cast<AXUIElementRef>(raw_parent);
    bool valid = !is_hidden(parent) && has_class(parent, @"_ComposerLayoutBody_");
    if (!valid && !is_hidden(parent) && has_class(parent, @"_RichTextInput_")) {
        CFTypeRef raw_body = copy_attribute(parent, kAXParentAttribute);
        if (raw_body && CFGetTypeID(raw_body) == AXUIElementGetTypeID()) {
            AXUIElementRef body = static_cast<AXUIElementRef>(raw_body);
            valid = !is_hidden(body) && has_class(body, @"_ComposerLayoutBody_");
        }
        if (raw_body) CFRelease(raw_body);
    }
    CFRelease(raw_parent);
    return valid;
}

AXUIElementRef composer_body(AXUIElementRef focused) {
    CFTypeRef raw_parent = copy_attribute(focused, kAXParentAttribute);
    if (!raw_parent || CFGetTypeID(raw_parent) != AXUIElementGetTypeID()) {
        if (raw_parent) CFRelease(raw_parent);
        return nullptr;
    }
    AXUIElementRef parent = static_cast<AXUIElementRef>(raw_parent);
    AXUIElementRef body = nullptr;
    if (!is_hidden(parent) && has_class(parent, @"_ComposerLayoutBody_")) {
        body = static_cast<AXUIElementRef>(CFRetain(parent));
    } else if (!is_hidden(parent) && has_class(parent, @"_RichTextInput_")) {
        CFTypeRef raw_body = copy_attribute(parent, kAXParentAttribute);
        if (raw_body && CFGetTypeID(raw_body) == AXUIElementGetTypeID()) {
            AXUIElementRef candidate = static_cast<AXUIElementRef>(raw_body);
            if (!is_hidden(candidate) && has_class(candidate, @"_ComposerLayoutBody_")) {
                body = static_cast<AXUIElementRef>(CFRetain(candidate));
            }
        }
        if (raw_body) CFRelease(raw_body);
    }
    CFRelease(raw_parent);
    return body;
}

int32_t semantic_for(AXUIElementRef button) {
    std::vector<int32_t> matches;
    const CFStringRef attributes[] = {kAXTitleAttribute, kAXDescriptionAttribute, kAXHelpAttribute};
    for (CFStringRef attribute : attributes) {
        CFTypeRef raw = copy_attribute(button, attribute);
        if (!is_string(raw)) { if (raw) CFRelease(raw); continue; }
        NSString *normalized = [[ns_string(static_cast<CFStringRef>(raw)) lowercaseString]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        while ([normalized rangeOfString:@"  "].location != NSNotFound) {
            normalized = [normalized stringByReplacingOccurrencesOfString:@"  " withString:@" "];
        }
        int32_t candidate = kUnknown;
        if ([normalized isEqualToString:@"send"] || [normalized isEqualToString:@"发送"]) candidate = kSend;
        if ([normalized isEqualToString:@"queue"] || [normalized isEqualToString:@"加入队列"]) candidate = kQueue;
        if ([normalized isEqualToString:@"steer"] || [normalized isEqualToString:@"调整方向"]) candidate = kSteer;
        if ([normalized isEqualToString:@"stop"] || [normalized isEqualToString:@"停止"]) candidate = kRunning;
        if (candidate != kUnknown && std::find(matches.begin(), matches.end(), candidate) == matches.end()) matches.push_back(candidate);
        CFRelease(raw);
    }
    return matches.size() == 1 ? matches.front() : kUnknown;
}

bool stable_token(AXUIElementRef element, CFTypeRef *output) {
    *output = nullptr;
    CFTypeRef token = copy_attribute(element, kChromeNodeId);
    if (!token) return false;
    const CFTypeID type = CFGetTypeID(token);
    if (type != CFStringGetTypeID() && type != CFNumberGetTypeID()) {
        CFRelease(token);
        return false;
    }
    *output = token;
    return true;
}

std::unique_ptr<PreparedTarget> find_target(pid_t pid, AXUIElementRef ax_app,
                                           Inspection *inspection) {
    if (!AXIsProcessTrustedWithOptions(nullptr)) {
        inspection->snapshot.accessibility_trusted = 0;
        return nullptr;
    }
    inspection->snapshot.accessibility_trusted = 1;
    CFTypeRef raw_focused = copy_attribute(ax_app, kAXFocusedUIElementAttribute);
    if (!raw_focused || CFGetTypeID(raw_focused) != AXUIElementGetTypeID()) {
        if (raw_focused) CFRelease(raw_focused);
        return nullptr;
    }
    AXUIElementRef focused = static_cast<AXUIElementRef>(raw_focused);
    if (!is_composer(focused)) { CFRelease(focused); return nullptr; }
    inspection->snapshot.editable = 1;

    CFTypeRef raw_value = copy_attribute(focused, kAXValueAttribute);
    if (raw_value && !is_string(raw_value)) {
        CFRelease(raw_value);
        CFRelease(focused);
        return nullptr;
    }
    if (!raw_value) {
        // AXValue is authoritative whenever it is available. Only when the
        // attribute is absent may a visible, exact `placeholder` class serve
        // as evidence that this is an empty composer.
        WalkResult placeholder_walk = descendants(focused);
        if (placeholder_walk.bounded) {
            CFRelease(focused);
            return nullptr;
        }
        bool visible_placeholder = false;
        for (size_t i = 1; i < placeholder_walk.elements.size(); ++i) {
            AXUIElementRef element = placeholder_walk.elements[i];
            if (visible_within_root(element, focused) && has_class(element, @"placeholder")) {
                visible_placeholder = true;
                break;
            }
        }
        if (!visible_placeholder) {
            CFRelease(focused);
            return nullptr;
        }
        raw_value = static_cast<CFTypeRef>(CFRetain(CFSTR("")));
    }
    NSString *value_string = ns_string(static_cast<CFStringRef>(raw_value));
    const bool has_text = [[value_string stringByTrimmingCharactersInSet:
        [NSCharacterSet whitespaceAndNewlineCharacterSet]] length] > 0;
    inspection->snapshot.has_text = has_text ? 1 : 0;

    AXUIElementRef body = composer_body(focused);
    if (!body) {
        CFRelease(raw_value);
        CFRelease(focused);
        return nullptr;
    }
    WalkResult walk = descendants(body);
    CFRelease(body);
    if (walk.bounded) {
        CFRelease(raw_value);
        CFRelease(focused);
        return nullptr;
    }

    std::vector<std::pair<AXUIElementRef, int32_t>> candidates;
    for (AXUIElementRef element : walk.elements) {
        CFTypeRef role = copy_attribute(element, kAXRoleAttribute);
        const bool is_button = string_equals(role, kAXButtonRole);
        if (role) CFRelease(role);
        if (!is_button) continue;
        const int32_t semantic = semantic_for(element);
        if (semantic != kUnknown) candidates.emplace_back(element, semantic);
    }
    if (candidates.size() != 1) {
        CFRelease(raw_value);
        CFRelease(focused);
        return nullptr;
    }
    AXUIElementRef button = candidates.front().first;
    const int32_t semantic = candidates.front().second;
    inspection->snapshot.semantic = semantic;
    if (is_hidden(button) || !is_enabled(button) || (!has_text && semantic != kRunning)) {
        CFRelease(raw_value);
        CFRelease(focused);
        return nullptr;
    }

    auto target = std::make_unique<PreparedTarget>();
    target->pid = pid;
    target->focused = focused; // consumes the owned reference
    target->button = static_cast<AXUIElementRef>(CFRetain(button));
    target->value = static_cast<CFStringRef>(raw_value); // consumes the owned reference
    target->semantic = semantic;
    if (!stable_token(focused, &target->focused_token) || !stable_token(button, &target->button_token)) {
        return nullptr;
    }
    return target;
}

Inspection inspect() {
    Inspection result;
    @autoreleasepool {
        NSRunningApplication *application = [NSWorkspace sharedWorkspace].frontmostApplication;
        if (!application || ![application.bundleIdentifier isEqualToString:@"com.openai.codex"]) return result;
        result.snapshot.codex_foreground = 1;
        const pid_t pid = application.processIdentifier;
        AXUIElementRef ax_app = AXUIElementCreateApplication(pid);
        if (!ax_app) return result;
        result.target = find_target(pid, ax_app, &result);
        CFRelease(ax_app);
    }
    return result;
}

bool same_target(const PreparedTarget &left, const PreparedTarget &right) {
    return left.pid == right.pid && left.semantic == right.semantic &&
        CFEqual(left.focused_token, right.focused_token) &&
        CFEqual(left.button_token, right.button_token) &&
        CFEqual(left.value, right.value);
}

bool same_composer(const PreparedTarget &left, const PreparedTarget &right) {
    return left.pid == right.pid && CFEqual(left.focused_token, right.focused_token);
}

bool restore_if_value_matches(const PreparedTarget &identity, CFStringRef expected_value,
                              CFStringRef original_value) {
    if (!expected_value || !original_value) return false;
    @autoreleasepool {
        auto focused_composer = [&identity](AXUIElementRef *focused_output,
                                            CFStringRef *value_output) -> bool {
            *focused_output = nullptr;
            *value_output = nullptr;
            NSRunningApplication *application = [NSWorkspace sharedWorkspace].frontmostApplication;
            if (!application || application.processIdentifier != identity.pid ||
                ![application.bundleIdentifier isEqualToString:@"com.openai.codex"]) return false;
            AXUIElementRef ax_app = AXUIElementCreateApplication(identity.pid);
            if (!ax_app) return false;
            CFTypeRef raw_focused = copy_attribute(ax_app, kAXFocusedUIElementAttribute);
            CFRelease(ax_app);
            if (!raw_focused || CFGetTypeID(raw_focused) != AXUIElementGetTypeID()) {
                if (raw_focused) CFRelease(raw_focused);
                return false;
            }
            AXUIElementRef focused = static_cast<AXUIElementRef>(raw_focused);
            CFTypeRef token = nullptr;
            if (!is_composer(focused) || !stable_token(focused, &token) ||
                !CFEqual(token, identity.focused_token)) {
                if (token) CFRelease(token);
                CFRelease(focused);
                return false;
            }
            CFRelease(token);
            CFTypeRef raw_value = copy_attribute(focused, kAXValueAttribute);
            if (!is_string(raw_value)) {
                if (raw_value) CFRelease(raw_value);
                CFRelease(focused);
                return false;
            }
            *focused_output = focused;
            *value_output = static_cast<CFStringRef>(raw_value);
            return true;
        };

        AXUIElementRef focused = nullptr;
        CFStringRef current_value = nullptr;
        if (!focused_composer(&focused, &current_value)) return false;
        const bool unchanged = CFEqual(current_value, expected_value);
        CFRelease(current_value);
        if (!unchanged) { CFRelease(focused); return false; }
        if (AXUIElementSetAttributeValue(focused, kAXValueAttribute, original_value) != kAXErrorSuccess) {
            CFRelease(focused);
            return false;
        }
        CFRelease(focused);

        AXUIElementRef restored_focused = nullptr;
        CFStringRef restored_value = nullptr;
        if (!focused_composer(&restored_focused, &restored_value)) return false;
        const bool restored = CFEqual(restored_value, original_value);
        CFRelease(restored_value);
        CFRelease(restored_focused);
        return restored;
    }
}

std::string encoded_path_component(const char *value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string output;
    for (const unsigned char byte : std::string(value)) {
        const bool ascii_alphanumeric = (byte >= 'a' && byte <= 'z') ||
            (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9');
        if (ascii_alphanumeric || byte == '-' || byte == '_' || byte == '.' || byte == '~') {
            output.push_back(static_cast<char>(byte));
        } else {
            output.push_back('%');
            output.push_back(kHex[byte >> 4]);
            output.push_back(kHex[byte & 0x0f]);
        }
    }
    return output;
}

} // namespace

extern "C" int32_t swax_prepare(SwaxSnapshot *output) {
    if (!output) return -1;
    *output = {};
    Inspection result = inspect();
    output->codex_foreground = result.snapshot.codex_foreground;
    output->accessibility_trusted = result.snapshot.accessibility_trusted;
    output->editable = result.snapshot.editable;
    output->has_text = result.snapshot.has_text;
    output->semantic = result.snapshot.semantic;
    if (result.target) {
        uint64_t handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        if (handle == 0) handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> lock(g_targets_mutex);
        g_targets.emplace(handle, std::move(result.target));
        output->handle = handle;
    }
    return 0;
}

extern "C" int32_t swax_dispatch(uint64_t handle, int32_t alternate) {
    if (handle == 0 || alternate < 0 || alternate > 2) return -1;
    std::unique_ptr<PreparedTarget> prepared;
    {
        std::lock_guard<std::mutex> lock(g_targets_mutex);
        auto found = g_targets.find(handle);
        if (found == g_targets.end()) return -2;
        prepared = std::move(found->second);
        g_targets.erase(found); // consume before the non-atomic AX/event boundary
    }
    Inspection fresh = inspect();
    if (!fresh.target || !same_target(*prepared, *fresh.target)) {
        if (prepared->original_value) {
            (void)restore_if_value_matches(*prepared, prepared->value, prepared->original_value);
        }
        return -3;
    }
    if (alternate) {
        if (prepared->semantic != kQueue && prepared->semantic != kRunning) {
            if (prepared->original_value) {
                (void)restore_if_value_matches(*prepared, prepared->value, prepared->original_value);
            }
            return -4;
        }
        CGEventRef key_down = CGEventCreateKeyboardEvent(nullptr, kReturnKeyCode, true);
        CGEventRef key_up = CGEventCreateKeyboardEvent(nullptr, kReturnKeyCode, false);
        if (!key_down || !key_up) {
            if (key_down) CFRelease(key_down);
            if (key_up) CFRelease(key_up);
            if (prepared->original_value) {
                (void)restore_if_value_matches(*prepared, prepared->value, prepared->original_value);
            }
            return -5;
        }
        CGEventFlags flags = kAlternateFlags;
        if (alternate == 2) flags |= kCGEventFlagMaskShift;
        CGEventSetFlags(key_down, flags);
        CGEventSetFlags(key_up, flags);
        CGEventPostToPid(prepared->pid, key_down);
        CGEventPostToPid(prepared->pid, key_up);
        CFRelease(key_down);
        CFRelease(key_up);
        return 0;
    }
    if (AXUIElementPerformAction(fresh.target->button, kAXPressAction) != kAXErrorSuccess) {
        if (prepared->original_value) {
            (void)restore_if_value_matches(*prepared, prepared->value, prepared->original_value);
        }
        return -6;
    }
    return 0;
}

extern "C" void swax_discard(uint64_t handle) {
    std::lock_guard<std::mutex> lock(g_targets_mutex);
    g_targets.erase(handle);
}

extern "C" int32_t swax_restore_value(uint64_t handle) {
    if (handle == 0) return -1;
    std::unique_ptr<PreparedTarget> prepared;
    {
        std::lock_guard<std::mutex> lock(g_targets_mutex);
        auto found = g_targets.find(handle);
        if (found == g_targets.end()) return -2;
        prepared = std::move(found->second);
        g_targets.erase(found);
    }
    if (!prepared->original_value) return -3;
    return restore_if_value_matches(*prepared, prepared->value, prepared->original_value) ? 0 : 1;
}

extern "C" int32_t swax_accessibility_trusted(void) {
    return AXIsProcessTrustedWithOptions(nullptr) ? 1 : 0;
}

extern "C" int32_t swvoice_typeless_running(void) {
    @autoreleasepool {
        for (NSRunningApplication *application in
             [[NSWorkspace sharedWorkspace] runningApplications]) {
            if ([application.bundleIdentifier isEqualToString:@"now.typeless.desktop"]) return 1;
        }
    }
    return 0;
}

extern "C" int32_t swvoice_post_key(uint16_t key_code, int32_t down) {
    if (key_code > 0x7e || (down != 0 && down != 1)) return -1;
    // Pair every successful key-down with its key-up even if Typeless exits
    // during the shortcut tap. Refusing key-up can leave a modifier stuck.
    if (down == 1 && !swvoice_typeless_running()) return -2;
    @autoreleasepool {
        CGEventRef event = CGEventCreateKeyboardEvent(
            nullptr, static_cast<CGKeyCode>(key_code), down == 1);
        if (!event) return -3;
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
    }
    return 0;
}

extern "C" int32_t swax_open_task(const char *thread_id) {
    if (!thread_id || !thread_id[0] || strnlen(thread_id, 257) > 256) return -1;
    for (const unsigned char byte : std::string(thread_id)) {
        if (std::iscntrl(byte)) return -1;
    }
    if (!AXIsProcessTrustedWithOptions(nullptr)) return -2;
    @autoreleasepool {
        const std::string escaped = encoded_path_component(thread_id);
        NSString *escaped_path = [[NSString alloc] initWithBytes:escaped.data()
                                                           length:escaped.size()
                                                         encoding:NSASCIIStringEncoding];
        if (!escaped_path) return -3;
        NSString *url_text = [NSString stringWithFormat:@"codex://threads/%@", escaped_path];
        NSURL *url = [NSURL URLWithString:url_text];
        if (!url || ![[NSWorkspace sharedWorkspace] openURL:url]) return -3;
    }
    return 0;
}

extern "C" int32_t swax_set_value(uint64_t handle, const char *raw_value,
                                     SwaxSnapshot *output) {
    if (handle == 0 || !raw_value || !raw_value[0] || !output) return -1;
    *output = {};
    const size_t length = strnlen(raw_value, 64 * 1024 + 1);
    if (length == 0 || length > 64 * 1024) return -1;

    std::unique_ptr<PreparedTarget> prepared;
    {
        std::lock_guard<std::mutex> lock(g_targets_mutex);
        auto found = g_targets.find(handle);
        if (found == g_targets.end()) return -2;
        prepared = std::move(found->second);
        g_targets.erase(found);
    }

    @autoreleasepool {
        if (!AXIsProcessTrustedWithOptions(nullptr)) return -3;
        Inspection current = inspect();
        if (!current.target || !same_target(*prepared, *current.target) ||
            current.snapshot.has_text != 0) return -4;
        NSString *text = [[NSString alloc] initWithBytes:raw_value length:length
                                                encoding:NSUTF8StringEncoding];
        if (!text) return -5;
        CFStringRef value = (__bridge CFStringRef)text;
        if (AXUIElementSetAttributeValue(current.target->focused, kAXValueAttribute, value) != kAXErrorSuccess) {
            (void)restore_if_value_matches(*current.target, value, current.target->value);
            return -6;
        }

        Inspection filled = inspect();
        if (filled.target) {
            filled.target->original_value = CFStringCreateCopy(kCFAllocatorDefault, current.target->value);
        }
        if (!filled.target || !filled.target->original_value || !same_composer(*current.target, *filled.target) ||
            filled.snapshot.has_text != 1 || !CFEqual(filled.target->value, value)) {
            if (filled.target && filled.target->original_value) {
                (void)restore_if_value_matches(*filled.target, value, filled.target->original_value);
            } else {
                (void)restore_if_value_matches(*current.target, value, current.target->value);
            }
            return -7;
        }
        output->codex_foreground = filled.snapshot.codex_foreground;
        output->accessibility_trusted = filled.snapshot.accessibility_trusted;
        output->editable = filled.snapshot.editable;
        output->has_text = filled.snapshot.has_text;
        output->semantic = filled.snapshot.semantic;
        uint64_t next_handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        if (next_handle == 0) next_handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(g_targets_mutex);
            g_targets.emplace(next_handle, std::move(filled.target));
        }
        output->handle = next_handle;
    }
    return 0;
}
