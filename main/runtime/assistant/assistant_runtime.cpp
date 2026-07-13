/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_runtime.h"

#include "hal/hal.h"
#include "runtime/resource_supervisor.h"
#include "runtime/service_runtime.h"
#include "runtime/services/shared_wifi_service.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <boost/json.hpp>
#include <brookesia/agent_helper/manager.hpp>
#include <brookesia/agent_helper/xiaozhi.hpp>
#include <brookesia/lib_utils/describe_helpers.hpp>
#include <brookesia/service_helper/audio.hpp>
#include <brookesia/service_manager.hpp>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <model_path.h>

namespace {
using AudioHelper = esp_brookesia::service::helper::Audio;
using ManagerHelper = esp_brookesia::agent::helper::Manager;
using XiaozhiHelper = esp_brookesia::agent::helper::XiaoZhi;
using SignalConnection = esp_brookesia::service::EventRegistry::SignalConnection;
using FunctionResult = esp_brookesia::service::FunctionResult;

constexpr const char* tag = "AssistantRuntime";
constexpr const char* assets_partition_label = "assets";
constexpr const char* srmodels_asset_name = "srmodels.bin";
constexpr uint32_t stop_timeout_ms = 2000;

struct __attribute__((packed)) AssetTableEntry {
    char name[32];
    uint32_t size;
    uint32_t offset;
    uint16_t width;
    uint16_t height;
};

static_assert(sizeof(AssetTableEntry) == 44);

const void* assets_mapping = nullptr;
esp_partition_mmap_handle_t assets_mapping_handle = 0;

uint32_t read_u32(const uint8_t* data)
{
    uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

bool load_unified_wake_models()
{
    if (get_static_srmodels() != nullptr) {
        return true;
    }

    const auto* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, assets_partition_label);
    if (partition == nullptr) {
        ESP_LOGE(tag, "assets partition not found");
        return false;
    }

    if (assets_mapping == nullptr) {
        const auto error = esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA,
                                              &assets_mapping, &assets_mapping_handle);
        if (error != ESP_OK) {
            ESP_LOGE(tag, "failed to map assets partition: %s", esp_err_to_name(error));
            return false;
        }
    }

    const auto* root = static_cast<const uint8_t*>(assets_mapping);
    const uint32_t file_count = read_u32(root);
    const uint32_t expected_checksum = read_u32(root + 4);
    const uint32_t stored_length = read_u32(root + 8);
    constexpr size_t header_size = 12;
    const size_t table_size = static_cast<size_t>(file_count) * sizeof(AssetTableEntry);
    if (stored_length == 0 || stored_length > partition->size - header_size ||
        table_size > stored_length) {
        ESP_LOGE(tag, "invalid assets header");
        return false;
    }

    uint32_t checksum = 0;
    for (size_t i = 0; i < stored_length; ++i) {
        checksum += root[header_size + i];
    }
    checksum &= 0xffff;
    if (checksum != expected_checksum) {
        ESP_LOGE(tag, "assets checksum mismatch: actual=%" PRIu32 " expected=%" PRIu32,
                 checksum, expected_checksum);
        return false;
    }

    const auto* table = reinterpret_cast<const AssetTableEntry*>(root + header_size);
    for (uint32_t i = 0; i < file_count; ++i) {
        if (std::strncmp(table[i].name, srmodels_asset_name, sizeof(table[i].name)) != 0) {
            continue;
        }

        const size_t payload_offset = header_size + table_size + table[i].offset;
        const size_t payload_end = payload_offset + 2 + table[i].size;
        if (payload_end > header_size + stored_length || root[payload_offset] != 0x5a ||
            root[payload_offset + 1] != 0x5a) {
            ESP_LOGE(tag, "invalid srmodels asset entry");
            return false;
        }

        if (srmodel_load(root + payload_offset + 2) == nullptr) {
            ESP_LOGE(tag, "failed to load srmodels asset");
            return false;
        }
        ESP_LOGI(tag, "loaded wake model from the assets partition");
        return true;
    }

    ESP_LOGE(tag, "srmodels.bin is missing from assets partition");
    return false;
}

std::string action_name(ManagerHelper::GeneralAction action)
{
    return BROOKESIA_DESCRIBE_TO_STR(action);
}

std::string event_name(ManagerHelper::GeneralEvent event)
{
    return BROOKESIA_DESCRIBE_TO_STR(event);
}

std::string manager_event_name(ManagerHelper::EventId event)
{
    return BROOKESIA_DESCRIBE_ENUM_TO_STR(event);
}

std::string xiaozhi_event_name(XiaozhiHelper::EventId event)
{
    return BROOKESIA_DESCRIBE_ENUM_TO_STR(event);
}
} // namespace

class AssistantRuntime::Impl {
public:
    AssistantStartResult start();
    bool requestStop();
    void update(uint32_t now_ms);

    AssistantStateMachine state;
    AssistantEventMailbox mailbox;

    WifiStaLease wifi;
    bool capture_owned = false;
    bool playback_owned = false;
    std::vector<std::shared_ptr<esp_brookesia::service::ServiceBase>> bindings;
    SignalConnection general_event;
    SignalConnection activation_code;
    SignalConnection user_text;
    SignalConnection agent_text;
    SignalConnection emote;

    std::atomic<uint64_t> callback_session{0};
    std::atomic<bool> callback_xiaozhi_selected{false};
    std::atomic<uint32_t> control_bits{0};
    std::atomic<bool> stop_submitted{false};
    uint32_t stop_started_ms = 0;
    TaskHandle_t runtime_task = nullptr;

    bool bindServices(uint64_t session_id);
    bool configureAudioAndAgent();
    bool subscribe(uint64_t session_id);
    bool submitActivate(uint64_t session_id);
    bool submitStart(uint64_t session_id);
    bool submitStop(uint64_t session_id);
    AssistantStartResult failAndReturn(uint64_t session_id, AssistantStartResult result,
                                       const char* message);
    void failStart(uint64_t session_id, const char* message);
    void releaseSession(uint64_t session_id);
    void disconnectSubscriptions();
    bool callbackIsCurrentXiaozhi(uint64_t session_id, const std::string& actual_event,
                                  const std::string& expected_event) const;
    bool submitAction(uint64_t session_id, ManagerHelper::GeneralAction action,
                      uint32_t failure_bit);

    std::size_t subscriptionCount() const;
    bool claimRuntimeThread();
    bool onRuntimeThread() const;

private:
    enum ControlBit : uint32_t {
        Activated = 1U << 0,
        Started = 1U << 1,
        Stopped = 1U << 2,
        StartAsyncFailed = 1U << 3,
        StopAsyncFailed = 1U << 4,
        UnexpectedLifecycle = 1U << 5,
    };
};

AssistantStartResult AssistantRuntime::Impl::start()
{
    if (state.state() != AssistantRuntimeState::Stopped) {
        return AssistantStartResult::AlreadyActive;
    }
    if (!claimRuntimeThread()) {
        ESP_LOGE(tag, "start called outside the Assistant runtime thread");
        return AssistantStartResult::ServiceUnavailable;
    }
    if (state.requestStart() != TransitionResult::Accepted) {
        return AssistantStartResult::AlreadyActive;
    }

    const uint64_t session = state.sessionId();
    callback_session.store(session);
    callback_xiaozhi_selected.store(false);
    control_bits.store(0);
    stop_submitted.store(false);
    mailbox.discardSessionsBefore(session);
    mailbox.publish({session, AssistantEventKind::State, "Starting"});

    if (!GetServiceRuntime().isStarted()) {
        return failAndReturn(session, AssistantStartResult::ServiceUnavailable,
                             "Service runtime unavailable");
    }
    if (!load_unified_wake_models()) {
        return failAndReturn(session, AssistantStartResult::ConfigurationFailed,
                             "Wake model unavailable");
    }

    wifi = GetSharedWifiService().acquireSta(RuntimeOwner::Assistant);
    if (!wifi.valid()) {
        return failAndReturn(session, AssistantStartResult::ResourceBusy, "Wi-Fi busy");
    }
    if (GetResourceSupervisor().acquireCapture(RuntimeOwner::Assistant) !=
        AcquireResult::Granted) {
        return failAndReturn(session, AssistantStartResult::ResourceBusy, "Microphone busy");
    }
    capture_owned = true;
    if (GetResourceSupervisor().acquirePlayback(RuntimeOwner::Assistant) !=
        AcquireResult::Granted) {
        return failAndReturn(session, AssistantStartResult::ResourceBusy, "Speaker busy");
    }
    playback_owned = true;
    if (!bindServices(session) || !configureAudioAndAgent() || !subscribe(session)) {
        return failAndReturn(session, AssistantStartResult::ConfigurationFailed,
                             "Service setup failed");
    }
    if (!submitActivate(session)) {
        return failAndReturn(session, AssistantStartResult::SubmitFailed,
                             "Activate submission failed");
    }
    mailbox.publish({session, AssistantEventKind::State, "Activating"});
    return AssistantStartResult::Submitted;
}

bool AssistantRuntime::Impl::requestStop()
{
    if (state.state() == AssistantRuntimeState::Stopped) {
        return true;
    }
    if (!onRuntimeThread()) {
        ESP_LOGE(tag, "stop called outside the Assistant runtime thread");
        return false;
    }

    const auto transition = state.requestStop();
    if (transition == TransitionResult::Rejected) {
        return false;
    }

    const uint64_t session = state.sessionId();
    if (transition == TransitionResult::Accepted) {
        stop_started_ms = GetHAL().millis();
        mailbox.publish({session, AssistantEventKind::State, "Stopping"});
    }
    if (stop_submitted.exchange(true)) {
        return true;
    }
    if (!submitStop(session)) {
        stop_submitted.store(false);
        return false;
    }
    return true;
}

void AssistantRuntime::Impl::update(uint32_t now_ms)
{
    if (state.state() == AssistantRuntimeState::Stopped && runtime_task == nullptr) {
        return;
    }
    if (!onRuntimeThread()) {
        ESP_LOGE(tag, "update called outside the Assistant runtime thread");
        return;
    }
    const uint64_t session = state.sessionId();
    const uint32_t signals = control_bits.exchange(0);

    if ((signals & Stopped) != 0) {
        releaseSession(session);
        return;
    }
    if ((signals & StartAsyncFailed) != 0) {
        failStart(session, "Agent start request failed");
        return;
    }
    if ((signals & StopAsyncFailed) != 0) {
        stop_submitted.store(false);
        mailbox.publish({session, AssistantEventKind::Error, "Stop request failed; retry available"});
    }
    if ((signals & UnexpectedLifecycle) != 0) {
        mailbox.publish({session, AssistantEventKind::Error, "Unexpected agent lifecycle event"});
    }
    if ((signals & Activated) != 0 && state.state() == AssistantRuntimeState::Starting) {
        mailbox.publish({session, AssistantEventKind::State, "Activated"});
        if (!submitStart(session)) {
            failStart(session, "Start submission failed");
            return;
        }
    }
    if ((signals & Started) != 0 &&
        state.markRunning(session) == TransitionResult::Accepted) {
        mailbox.publish({session, AssistantEventKind::State, "Running"});
    }

    if (state.state() == AssistantRuntimeState::Stopping &&
        static_cast<uint32_t>(now_ms - stop_started_ms) >= stop_timeout_ms) {
        if (state.markStopTimedOut(session) == TransitionResult::Accepted) {
            GetResourceSupervisor().block();
            stop_submitted.store(false);
            mailbox.publish({session, AssistantEventKind::Error,
                             "Stop timed out; resources remain blocked"});
        }
    }
}

bool AssistantRuntime::Impl::bindServices(uint64_t session_id)
{
    if (callback_session.load() != session_id) {
        return false;
    }
    auto& manager = esp_brookesia::service::ServiceManager::get_instance();
    const std::array<std::string_view, 3> names = {
        AudioHelper::get_name(), ManagerHelper::get_name(), XiaozhiHelper::get_name()};
    bindings.clear();
    for (const auto name : names) {
        auto service = manager.get_service(std::string(name));
        if (service == nullptr) {
            ESP_LOGE(tag, "required service is unavailable: %.*s", static_cast<int>(name.size()),
                     name.data());
            bindings.clear();
            return false;
        }
        bindings.push_back(std::move(service));
    }
    return true;
}

bool AssistantRuntime::Impl::configureAudioAndAgent()
{
    AudioHelper::AFE_Config afe_config{
        .vad = AudioHelper::AFE_VAD_Config{},
        .wakenet = AudioHelper::AFE_WakeNetConfig{
            .model_partition_label = "assets",
            .mn_language = "cn",
        },
    };
    auto afe_result = AudioHelper::call_function_sync(
        AudioHelper::FunctionId::SetAFE_Config,
        BROOKESIA_DESCRIBE_TO_JSON(afe_config).as_object());
    if (!afe_result) {
        ESP_LOGE(tag, "failed to configure AFE: %s", afe_result.error().c_str());
        return false;
    }

    const double volume = static_cast<double>(GetHAL().getSpeakerVolume());
    auto volume_result = AudioHelper::call_function_sync(AudioHelper::FunctionId::SetVolume, volume);
    if (!volume_result) {
        ESP_LOGE(tag, "failed to map current volume: %s", volume_result.error().c_str());
        return false;
    }

    const std::string agent_name(XiaozhiHelper::get_name());
    boost::json::object info{
        {"product", "M5Stack StopWatch"},
        {"language", "zh-CN"},
        {"audio", "opus/16000/mono"},
        {"identity_source", "existing board NVS"},
    };
    auto info_result = ManagerHelper::call_function_sync(
        ManagerHelper::FunctionId::SetAgentInfo, agent_name, std::move(info));
    if (!info_result) {
        ESP_LOGE(tag, "failed to set Xiaozhi agent info: %s", info_result.error().c_str());
        return false;
    }

    auto target_result = ManagerHelper::call_function_sync(
        ManagerHelper::FunctionId::SetTargetAgent, agent_name);
    if (!target_result) {
        ESP_LOGE(tag, "failed to select Xiaozhi agent: %s", target_result.error().c_str());
        return false;
    }
    callback_xiaozhi_selected.store(true);
    return true;
}

bool AssistantRuntime::Impl::callbackIsCurrentXiaozhi(
    uint64_t session_id, const std::string& actual_event, const std::string& expected_event) const
{
    // Brookesia 0.7 manager events do not carry an agent name. SetTargetAgent above makes Xiaozhi
    // the only event source, and this flag plus the exact helper event name rejects stale/foreign input.
    return callback_session.load() == session_id && callback_xiaozhi_selected.load() &&
           actual_event == expected_event;
}

bool AssistantRuntime::Impl::subscribe(uint64_t session_id)
{
    general_event = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::GeneralEventHappened,
        [this, session_id](const std::string& source, const std::string& event, bool unexpected) {
            if (!callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::GeneralEventHappened))) {
                return;
            }
            uint32_t bits = unexpected ? static_cast<uint32_t>(UnexpectedLifecycle) : 0U;
            if (event == event_name(ManagerHelper::GeneralEvent::Activated)) {
                bits |= Activated;
            } else if (event == event_name(ManagerHelper::GeneralEvent::Started)) {
                bits |= Started;
            } else if (event == event_name(ManagerHelper::GeneralEvent::Stopped)) {
                bits |= Stopped;
            }
            if (bits != 0) {
                control_bits.fetch_or(bits);
            }
        });
    activation_code = XiaozhiHelper::subscribe_event(
        XiaozhiHelper::EventId::ActivationCodeReceived,
        [this, session_id](const std::string& source, const std::string& code) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    xiaozhi_event_name(XiaozhiHelper::EventId::ActivationCodeReceived))) {
                mailbox.publish({session_id, AssistantEventKind::ActivationCode, code});
            }
        });
    user_text = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::UserSpeakingTextGot,
        [this, session_id](const std::string& source, const std::string& text) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::UserSpeakingTextGot))) {
                mailbox.publish({session_id, AssistantEventKind::UserText, text});
            }
        });
    agent_text = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::AgentSpeakingTextGot,
        [this, session_id](const std::string& source, const std::string& text) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::AgentSpeakingTextGot))) {
                mailbox.publish({session_id, AssistantEventKind::AgentText, text});
            }
        });
    emote = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::EmoteGot,
        [this, session_id](const std::string& source, const std::string& emotion) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::EmoteGot))) {
                mailbox.publish({session_id, AssistantEventKind::Emotion, emotion});
            }
        });

    return general_event.connected() && activation_code.connected() && user_text.connected() &&
           agent_text.connected() && emote.connected();
}

bool AssistantRuntime::Impl::submitAction(uint64_t session_id,
                                          ManagerHelper::GeneralAction action,
                                          uint32_t failure_bit)
{
    const std::string name = action_name(action);
    const bool submitted = ManagerHelper::call_function_async(
        ManagerHelper::FunctionId::TriggerGeneralAction, name,
        [this, session_id, failure_bit](FunctionResult&& result) {
            if (callback_session.load() != session_id || result.success) {
                return;
            }
            control_bits.fetch_or(failure_bit);
        });
    if (!submitted) {
        ESP_LOGE(tag, "failed to submit %s request", name.c_str());
    }
    return submitted;
}

bool AssistantRuntime::Impl::submitActivate(uint64_t session_id)
{
    return submitAction(session_id, ManagerHelper::GeneralAction::Activate, StartAsyncFailed);
}

bool AssistantRuntime::Impl::submitStart(uint64_t session_id)
{
    return submitAction(session_id, ManagerHelper::GeneralAction::Start, StartAsyncFailed);
}

bool AssistantRuntime::Impl::submitStop(uint64_t session_id)
{
    return submitAction(session_id, ManagerHelper::GeneralAction::Stop, StopAsyncFailed);
}

AssistantStartResult AssistantRuntime::Impl::failAndReturn(
    uint64_t session_id, AssistantStartResult result, const char* message)
{
    mailbox.publish({session_id, AssistantEventKind::Error, message});
    state.markFailed(session_id);
    state.requestStop();
    releaseSession(session_id);
    return result;
}

void AssistantRuntime::Impl::failStart(uint64_t session_id, const char* message)
{
    mailbox.publish({session_id, AssistantEventKind::Error, message});
    state.markFailed(session_id);
    state.requestStop();
    releaseSession(session_id);
}

void AssistantRuntime::Impl::disconnectSubscriptions()
{
    callback_session.store(0);
    callback_xiaozhi_selected.store(false);
    general_event.disconnect();
    activation_code.disconnect();
    user_text.disconnect();
    agent_text.disconnect();
    emote.disconnect();
}

void AssistantRuntime::Impl::releaseSession(uint64_t session_id)
{
    if (session_id != state.sessionId()) {
        return;
    }
    if (state.state() == AssistantRuntimeState::Starting ||
        state.state() == AssistantRuntimeState::Running ||
        state.state() == AssistantRuntimeState::Failed) {
        state.requestStop();
    }

    disconnectSubscriptions();
    bindings.clear();
    if (playback_owned) {
        GetResourceSupervisor().releasePlayback(RuntimeOwner::Assistant);
    }
    if (capture_owned) {
        GetResourceSupervisor().releaseCapture(RuntimeOwner::Assistant);
    }
    playback_owned = false;
    capture_owned = false;
    wifi.reset();
    GetResourceSupervisor().unblock();
    stop_submitted.store(false);
    control_bits.store(0);
    state.markStopped(session_id);
    mailbox.publish({session_id, AssistantEventKind::State, "Stopped"});
    runtime_task = nullptr;
}

bool AssistantRuntime::Impl::claimRuntimeThread()
{
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    if (runtime_task == nullptr && state.state() == AssistantRuntimeState::Stopped) {
        runtime_task = current;
    }
    return runtime_task == current;
}

bool AssistantRuntime::Impl::onRuntimeThread() const
{
    return runtime_task != nullptr && runtime_task == xTaskGetCurrentTaskHandle();
}

std::size_t AssistantRuntime::Impl::subscriptionCount() const
{
    return static_cast<std::size_t>(general_event.connected()) +
           static_cast<std::size_t>(activation_code.connected()) +
           static_cast<std::size_t>(user_text.connected()) +
           static_cast<std::size_t>(agent_text.connected()) +
           static_cast<std::size_t>(emote.connected());
}

AssistantRuntime::AssistantRuntime() : _impl(std::make_unique<Impl>())
{
}

AssistantRuntime::~AssistantRuntime() = default;

AssistantStartResult AssistantRuntime::start()
{
    return _impl->start();
}

bool AssistantRuntime::requestStop()
{
    return _impl->requestStop();
}

void AssistantRuntime::update(uint32_t now_ms)
{
    _impl->update(now_ms);
}

AssistantRuntimeState AssistantRuntime::state() const
{
    return _impl->state.state();
}

uint64_t AssistantRuntime::sessionId() const
{
    return _impl->state.sessionId();
}

bool AssistantRuntime::pollEvent(AssistantEvent& event)
{
    return _impl->mailbox.poll(event);
}

std::size_t AssistantRuntime::bindingCount() const
{
    return _impl->bindings.size();
}

std::size_t AssistantRuntime::subscriptionCount() const
{
    return _impl->subscriptionCount();
}

AssistantRuntime& GetAssistantRuntime()
{
    static AssistantRuntime runtime;
    return runtime;
}
