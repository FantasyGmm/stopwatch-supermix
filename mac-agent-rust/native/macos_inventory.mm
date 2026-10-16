#include "macos_inventory.h"

#include <CoreServices/CoreServices.h>
#include <limits.h>
#include <sys/stat.h>

namespace {

bool application_url_matches(CFURLRef url, CFStringRef expected_bundle_id) {
    if (!url) return false;
    CFStringRef scheme = CFURLCopyScheme(url);
    if (!scheme) return false;
    const bool is_file = CFStringCompare(scheme, CFSTR("file"), kCFCompareCaseInsensitive)
        == kCFCompareEqualTo;
    CFRelease(scheme);
    if (!is_file) return false;

    UInt8 path[PATH_MAX];
    if (!CFURLGetFileSystemRepresentation(url, true, path, sizeof(path))) return false;

    struct stat info {};
    if (stat(reinterpret_cast<const char *>(path), &info) != 0 || !S_ISDIR(info.st_mode)) {
        return false;
    }

    CFBundleRef bundle = CFBundleCreate(kCFAllocatorDefault, url);
    if (!bundle) return false;
    CFStringRef actual_bundle_id = CFBundleGetIdentifier(bundle);
    const bool matches = actual_bundle_id
        && CFStringCompare(actual_bundle_id, expected_bundle_id, 0) == kCFCompareEqualTo;
    CFRelease(bundle);
    return matches;
}

bool registered_application_exists(CFStringRef bundle_id, bool *lookup_ok) {
    CFErrorRef error = nullptr;
    CFArrayRef urls = LSCopyApplicationURLsForBundleIdentifier(bundle_id, &error);
    if (error) {
        if (urls) CFRelease(urls);
        const bool not_found = CFErrorGetCode(error) == kLSApplicationNotFoundErr;
        CFRelease(error);
        if (not_found) {
            *lookup_ok = true;
            return false;
        }
        *lookup_ok = false;
        return false;
    }
    *lookup_ok = true;
    bool found = false;
    if (urls) {
        const CFIndex count = CFArrayGetCount(urls);
        for (CFIndex index = 0; index < count; ++index) {
            const auto url = static_cast<CFURLRef>(CFArrayGetValueAtIndex(urls, index));
            if (application_url_matches(url, bundle_id)) {
                found = true;
                break;
            }
        }
        CFRelease(urls);
    }
    return found;
}

}  // namespace

extern "C" int32_t sw_detect_installed_agents(uint8_t *mask) {
    if (!mask) return -1;
    *mask = 0;

    bool lookup_ok = false;
    if (registered_application_exists(CFSTR("com.openai.codex"), &lookup_ok)) {
        *mask |= 0b01;
    } else if (!lookup_ok) {
        return -1;
    }

    if (registered_application_exists(CFSTR("cn.trae.app"), &lookup_ok)) {
        *mask |= 0b10;
    } else if (!lookup_ok) {
        return -1;
    }
    return 1;
}
