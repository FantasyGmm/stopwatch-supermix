#include "voice_audio_bridge.h"

#include "voice_audio_ring.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr int kInvalidArgument = -1;
constexpr int kDeviceNotFound = -2;
constexpr int kInvalidState = -3;
constexpr size_t kFramesPerBuffer = 960;
constexpr size_t kBufferCount = 3;
constexpr size_t kBytesPerFrame = sizeof(int16_t) * 2;

VoiceAudioRing<48000> audio_ring;
AudioQueueRef audio_queue = nullptr;
std::array<AudioQueueBufferRef, kBufferCount> audio_buffers{};

int copyString(CFStringRef value, char* output, size_t output_size)
{
    if (value == nullptr || output == nullptr || output_size == 0) {
        return kInvalidArgument;
    }
    if (!CFStringGetCString(
            value,
            output,
            static_cast<CFIndex>(output_size),
            kCFStringEncodingUTF8)) {
        output[0] = '\0';
        return kInvalidArgument;
    }
    return 0;
}

OSStatus getAudioDevices(std::vector<AudioDeviceID>& devices)
{
    AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 byte_count = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject,
        &address,
        0,
        nullptr,
        &byte_count);
    if (status != noErr) {
        return status;
    }

    devices.resize(byte_count / sizeof(AudioDeviceID));
    return AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &address,
        0,
        nullptr,
        &byte_count,
        devices.data());
}

OSStatus getDeviceString(
    AudioDeviceID device,
    AudioObjectPropertySelector selector,
    CFStringRef* value)
{
    AudioObjectPropertyAddress address = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 value_size = sizeof(*value);
    return AudioObjectGetPropertyData(
        device,
        &address,
        0,
        nullptr,
        &value_size,
        value);
}

int findDeviceByUid(const char* uid, AudioDeviceID* output)
{
    if (uid == nullptr || uid[0] == '\0' || output == nullptr) {
        return kInvalidArgument;
    }

    CFStringRef target_uid = CFStringCreateWithCString(
        kCFAllocatorDefault,
        uid,
        kCFStringEncodingUTF8);
    if (target_uid == nullptr) {
        return kInvalidArgument;
    }

    std::vector<AudioDeviceID> devices;
    const OSStatus devices_status = getAudioDevices(devices);
    if (devices_status != noErr) {
        CFRelease(target_uid);
        return static_cast<int>(devices_status);
    }

    int result = kDeviceNotFound;
    for (const AudioDeviceID device : devices) {
        CFStringRef device_uid = nullptr;
        if (getDeviceString(device, kAudioDevicePropertyDeviceUID, &device_uid) != noErr ||
            device_uid == nullptr) {
            continue;
        }
        const bool matches =
            CFStringCompare(device_uid, target_uid, 0) == kCFCompareEqualTo;
        CFRelease(device_uid);
        if (matches) {
            *output = device;
            result = 0;
            break;
        }
    }

    CFRelease(target_uid);
    return result;
}

void audioQueueCallback(
    void*,
    AudioQueueRef queue,
    AudioQueueBufferRef buffer)
{
    const size_t frame_capacity =
        buffer->mAudioDataBytesCapacity / kBytesPerFrame;
    auto* output = static_cast<int16_t*>(buffer->mAudioData);
    const size_t frames_read = audio_ring.readStereo(output, frame_capacity);
    std::fill(
        output + frames_read * 2,
        output + frame_capacity * 2,
        static_cast<int16_t>(0));
    buffer->mAudioDataByteSize =
        static_cast<UInt32>(frame_capacity * kBytesPerFrame);
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

void disposeAudioQueue()
{
    if (audio_queue != nullptr) {
        AudioQueueDispose(audio_queue, true);
        audio_queue = nullptr;
    }
    audio_buffers.fill(nullptr);
    audio_ring.clear();
}

}  // namespace

int swva_find_device_uid(const char* name, char* output, size_t output_size)
{
    if (name == nullptr || name[0] == '\0' || output == nullptr || output_size == 0) {
        return kInvalidArgument;
    }
    output[0] = '\0';

    CFStringRef target_name = CFStringCreateWithCString(
        kCFAllocatorDefault,
        name,
        kCFStringEncodingUTF8);
    if (target_name == nullptr) {
        return kInvalidArgument;
    }

    std::vector<AudioDeviceID> devices;
    const OSStatus devices_status = getAudioDevices(devices);
    if (devices_status != noErr) {
        CFRelease(target_name);
        return static_cast<int>(devices_status);
    }

    int result = kDeviceNotFound;
    for (const AudioDeviceID device : devices) {
        CFStringRef device_name = nullptr;
        if (getDeviceString(device, kAudioObjectPropertyName, &device_name) != noErr ||
            device_name == nullptr) {
            continue;
        }
        const bool matches =
            CFStringCompare(device_name, target_name, 0) == kCFCompareEqualTo;
        CFRelease(device_name);
        if (!matches) {
            continue;
        }

        CFStringRef device_uid = nullptr;
        const OSStatus uid_status =
            getDeviceString(device, kAudioDevicePropertyDeviceUID, &device_uid);
        if (uid_status == noErr && device_uid != nullptr) {
            result = copyString(device_uid, output, output_size);
            CFRelease(device_uid);
        } else {
            result = uid_status == noErr
                ? kInvalidState
                : static_cast<int>(uid_status);
        }
        break;
    }

    CFRelease(target_name);
    return result;
}

int swva_get_default_input_uid(char* output, size_t output_size)
{
    if (output == nullptr || output_size == 0) {
        return kInvalidArgument;
    }
    output[0] = '\0';

    AudioObjectPropertyAddress default_input_address = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    AudioDeviceID device = kAudioObjectUnknown;
    UInt32 device_size = sizeof(device);
    const OSStatus device_status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &default_input_address,
        0,
        nullptr,
        &device_size,
        &device);
    if (device_status != noErr) {
        return static_cast<int>(device_status);
    }

    CFStringRef device_uid = nullptr;
    const OSStatus uid_status =
        getDeviceString(device, kAudioDevicePropertyDeviceUID, &device_uid);
    if (uid_status != noErr || device_uid == nullptr) {
        return uid_status == noErr
            ? kInvalidState
            : static_cast<int>(uid_status);
    }

    const int result = copyString(device_uid, output, output_size);
    CFRelease(device_uid);
    return result;
}

int swva_set_default_input_uid(const char* uid)
{
    AudioDeviceID device = kAudioObjectUnknown;
    const int find_result = findDeviceByUid(uid, &device);
    if (find_result != 0) {
        return find_result;
    }

    AudioObjectPropertyAddress default_input_address = {
        kAudioHardwarePropertyDefaultInputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    return static_cast<int>(AudioObjectSetPropertyData(
        kAudioObjectSystemObject,
        &default_input_address,
        0,
        nullptr,
        sizeof(device),
        &device));
}

int swva_open(const char* output_device_uid)
{
    if (output_device_uid == nullptr || output_device_uid[0] == '\0') {
        return kInvalidArgument;
    }
    if (audio_queue != nullptr) {
        return kInvalidState;
    }

    AudioStreamBasicDescription format{};
    format.mSampleRate = 48000.0;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket = static_cast<UInt32>(kBytesPerFrame);
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = static_cast<UInt32>(kBytesPerFrame);
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 16;

    OSStatus status = AudioQueueNewOutput(
        &format,
        audioQueueCallback,
        nullptr,
        nullptr,
        nullptr,
        0,
        &audio_queue);
    if (status != noErr) {
        audio_queue = nullptr;
        return static_cast<int>(status);
    }

    CFStringRef device_uid = CFStringCreateWithCString(
        kCFAllocatorDefault,
        output_device_uid,
        kCFStringEncodingUTF8);
    if (device_uid == nullptr) {
        disposeAudioQueue();
        return kInvalidArgument;
    }
    status = AudioQueueSetProperty(
        audio_queue,
        kAudioQueueProperty_CurrentDevice,
        &device_uid,
        sizeof(device_uid));
    CFRelease(device_uid);
    if (status != noErr) {
        disposeAudioQueue();
        return static_cast<int>(status);
    }

    audio_ring.clear();
    for (AudioQueueBufferRef& buffer : audio_buffers) {
        status = AudioQueueAllocateBuffer(
            audio_queue,
            static_cast<UInt32>(kFramesPerBuffer * kBytesPerFrame),
            &buffer);
        if (status != noErr) {
            disposeAudioQueue();
            return static_cast<int>(status);
        }
        std::memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
        status = AudioQueueEnqueueBuffer(audio_queue, buffer, 0, nullptr);
        if (status != noErr) {
            disposeAudioQueue();
            return static_cast<int>(status);
        }
    }

    return 0;
}

int swva_start(void)
{
    if (audio_queue == nullptr) {
        return kInvalidState;
    }
    return static_cast<int>(AudioQueueStart(audio_queue, nullptr));
}

size_t swva_write_mono_16k(const int16_t* samples, size_t count)
{
    return audio_ring.writeMono16kAsStereo48k(samples, count);
}

uint64_t swva_dropped_frames(void)
{
    return audio_ring.droppedFrames();
}

void swva_stop(void)
{
    if (audio_queue != nullptr) {
        AudioQueueStop(audio_queue, true);
    }
    audio_ring.clear();
}

void swva_close(void)
{
    if (audio_queue != nullptr) {
        AudioQueueStop(audio_queue, true);
    }
    disposeAudioQueue();
}
