/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_runtime.h"
#include "assistant_stop_policy.h"
#include "assistant_trace.h"
#include "assistant_update_notice_bridge.h"
#include "assistant_update_notice_policy.h"
#include "assistant_update_notice_store.h"

#include "apps/common/common.h"
#include "hal/hal.h"
#include "runtime/resource_supervisor.h"
#include "runtime/runtime_diagnostics.h"
#include "runtime/service_runtime.h"
#include "runtime/services/shared_wifi_service.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <exception>
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
#include <brookesia/service_helper/nvs.hpp>
#include <brookesia/service_manager.hpp>
#include <esp_log.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <model_path.h>

namespace {
using AudioHelper = esp_brookesia::service::helper::Audio;
using ManagerHelper = esp_brookesia::agent::helper::Manager;
using NVSHelper = esp_brookesia::service::helper::NVS;
using XiaozhiHelper = esp_brookesia::agent::helper::XiaoZhi;
using SignalConnection = esp_brookesia::service::EventRegistry::SignalConnection;
using FunctionResult = esp_brookesia::service::FunctionResult;

constexpr const char* tag = "AssistantRuntime";
constexpr const char* assets_partition_label = "assets";
constexpr const char* srmodels_asset_name = "srmodels.bin";
constexpr uint32_t stop_timeout_ms = 2000;
constexpr uint32_t wifi_connect_timeout_ms = 60000;

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

AssistantTraceDetail action_detail(ManagerHelper::GeneralAction action)
{
    switch (action) {
        case ManagerHelper::GeneralAction::Activate:
            return AssistantTraceDetail::Activate;
        case ManagerHelper::GeneralAction::Start:
            return AssistantTraceDetail::Start;
        case ManagerHelper::GeneralAction::Stop:
            return AssistantTraceDetail::Stop;
        default:
            return AssistantTraceDetail::Unexpected;
    }
}

void record_trace(uint64_t session_id, AssistantTraceComponent component,
                  AssistantTraceEvent event, AssistantRuntimeState state,
                  AssistantTraceDetail detail = AssistantTraceDetail::None,
                  AssistantTraceResult result = AssistantTraceResult::None,
                  int32_t value0 = 0, int32_t value1 = 0,
                  int32_t value2 = 0, int32_t value3 = 0)
{
    GetAssistantTrace().record({
        .session_id = session_id,
        .timestamp_ms = GetHAL().millis(),
        .component = component,
        .event = event,
        .state = state,
        .detail = detail,
        .result = result,
        .value0 = value0,
        .value1 = value1,
        .value2 = value2,
        .value3 = value3,
    });
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

void log_memory_stage(const char* boundary)
{
    const RuntimeDiagnosticSnapshot snapshot = CaptureRuntimeDiagnosticSnapshot();
    ESP_LOGI(tag, "memory %s internal=%u/%u largest=%u psram=%u tasks=%u", boundary,
             static_cast<unsigned>(snapshot.internal_free_heap),
             static_cast<unsigned>(snapshot.internal_min_free_heap),
             static_cast<unsigned>(snapshot.internal_largest_free_block),
             static_cast<unsigned>(snapshot.psram_free),
             static_cast<unsigned>(snapshot.task_count));
}
} // namespace

class AssistantRuntime::Impl {
public:
    AssistantStartResult start();
    bool requestStop();
    void update(uint32_t now_ms);

    AssistantStateMachine state;
    AssistantEventMailbox mailbox;
    AssistantUpdateNoticeModel update_notice{common::FirmwareVersion};
    AssistantUpdateNoticeStore update_notice_store;

    WifiStaLease wifi;
    bool capture_owned = false;
    bool playback_owned = false;
    std::vector<esp_brookesia::service::ServiceBinding> bindings;
    SignalConnection general_event;
    SignalConnection activation_code;
    SignalConnection user_text;
    SignalConnection agent_text;
    SignalConnection listening_status;
    SignalConnection speaking_status;
    SignalConnection emote;

    std::atomic<uint64_t> callback_session{0};
    std::atomic<bool> callback_xiaozhi_selected{false};
    std::atomic<uint32_t> control_bits{0};
    std::atomic<bool> stop_submitted{false};
    uint32_t stop_started_ms = 0;
    uint32_t wifi_connect_started_ms = 0;
    TaskHandle_t runtime_task = nullptr;
    bool service_setup_pending = false;

    bool finishStart(uint64_t session_id);
    bool configurePlaybackBeforeStart();
    bool bindServices(uint64_t session_id);
    bool configureAudioAndAgent();
    bool subscribe(uint64_t session_id);
    bool submitActivate(uint64_t session_id);
    bool submitStart(uint64_t session_id);
    bool submitSleep(uint64_t session_id);
    bool submitStop(uint64_t session_id);
    AssistantStartResult failAndReturn(uint64_t session_id, AssistantStartResult result,
                                       const char* message);
    void failStart(uint64_t session_id, const char* message);
    void releaseSession(uint64_t session_id);
    void disconnectSubscriptions();
    void restoreUpdateNotice(uint64_t session_id);
    void consumeUpdateNoticeReport(uint64_t session_id);
    void applyUpdateStorageAction(AssistantUpdateStorageAction action);
    void publishUpdateNotice(uint64_t session_id);
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
        record_trace(state.sessionId(), AssistantTraceComponent::Runtime,
                     AssistantTraceEvent::StartRequest, state.state(),
                     AssistantTraceDetail::Start, AssistantTraceResult::Busy);
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
    record_trace(session, AssistantTraceComponent::Runtime,
                 AssistantTraceEvent::StartRequest, state.state(),
                 AssistantTraceDetail::Start, AssistantTraceResult::Accepted);
    callback_session.store(session);
    callback_xiaozhi_selected.store(false);
    control_bits.store(0);
    stop_submitted.store(false);
    GetAssistantUpdateNoticeBridge().beginSession(session);
    mailbox.discardSessionsBefore(session);
    mailbox.publish({session, AssistantEventKind::State, "Starting"});

    try {
        restoreUpdateNotice(session);
        log_memory_stage("before-resources");
        if (!GetServiceRuntime().isStarted()) {
            return failAndReturn(session, AssistantStartResult::ServiceUnavailable,
                                 "Service runtime unavailable");
        }
        auto wifi_acquire =
            GetSharedWifiService().acquireConfiguredStaDetailed(RuntimeOwner::Assistant);
        const bool wifi_acquired =
            wifi_acquire.result == WifiConfiguredStaAcquireResult::Acquired;
        record_trace(session, AssistantTraceComponent::Resource,
                     AssistantTraceEvent::ResourceDecision, state.state(),
                     AssistantTraceDetail::Wifi,
                     wifi_acquired ? AssistantTraceResult::Accepted
                                   : wifi_acquire.result == WifiConfiguredStaAcquireResult::Busy
                                         ? AssistantTraceResult::Busy
                                         : AssistantTraceResult::Failed,
                     static_cast<int32_t>(GetResourceSupervisor().wifiLeaseCount()));
        if (wifi_acquire.result == WifiConfiguredStaAcquireResult::NoCredentials) {
            return failAndReturn(session, AssistantStartResult::ConfigurationFailed,
                                 "Configure Wi-Fi in Settings");
        }
        if (wifi_acquire.result == WifiConfiguredStaAcquireResult::Busy) {
            return failAndReturn(session, AssistantStartResult::ResourceBusy, "Wi-Fi busy");
        }
        if (wifi_acquire.result == WifiConfiguredStaAcquireResult::StartFailed) {
            return failAndReturn(session, AssistantStartResult::ServiceUnavailable,
                                 "Wi-Fi unavailable");
        }
        wifi = std::move(wifi_acquire.lease);
        log_memory_stage("after-wifi");
        service_setup_pending = true;
        wifi_connect_started_ms = GetHAL().millis();
        mailbox.publish({session, AssistantEventKind::State, "Connecting"});
        return AssistantStartResult::Submitted;
    } catch (const std::exception& error) {
        ESP_LOGE(tag, "exception during Assistant setup: %s", error.what());
        log_memory_stage("setup-exception");
        return failAndReturn(session, AssistantStartResult::ConfigurationFailed, "Setup failed");
    } catch (...) {
        ESP_LOGE(tag, "unknown exception during Assistant setup");
        log_memory_stage("setup-exception");
        return failAndReturn(session, AssistantStartResult::ConfigurationFailed, "Setup failed");
    }
}

bool AssistantRuntime::Impl::requestStop()
{
    if (state.state() == AssistantRuntimeState::Stopped) {
        record_trace(state.sessionId(), AssistantTraceComponent::Runtime,
                     AssistantTraceEvent::StateTransition, state.state(),
                     AssistantTraceDetail::Stop, AssistantTraceResult::Duplicate);
        return true;
    }
    if (!onRuntimeThread()) {
        ESP_LOGE(tag, "stop called outside the Assistant runtime thread");
        return false;
    }

    const auto transition = state.requestStop();
    if (transition == TransitionResult::Rejected) {
        record_trace(state.sessionId(), AssistantTraceComponent::Runtime,
                     AssistantTraceEvent::StateTransition, state.state(),
                     AssistantTraceDetail::Stop, AssistantTraceResult::Rejected);
        return false;
    }

    const uint64_t session = state.sessionId();
    record_trace(session, AssistantTraceComponent::Runtime,
                 AssistantTraceEvent::StateTransition, state.state(),
                 AssistantTraceDetail::Stop,
                 transition == TransitionResult::Accepted
                     ? AssistantTraceResult::Accepted
                     : AssistantTraceResult::Duplicate);
    if (transition == TransitionResult::Accepted) {
        stop_started_ms = GetHAL().millis();
        mailbox.publish({session, AssistantEventKind::State, "Stopping"});
    }
    const AssistantStopPath stop_path = DecideAssistantStopPath(
        service_setup_pending, GetSharedWifiService().staConnected());
    if (stop_path == AssistantStopPath::ReleasePendingSetup) {
        releaseSession(session);
        return true;
    }
    if (stop_path == AssistantStopPath::WaitForOfflineAgentStop) {
        // A transport loss makes Brookesia force its own Stop action. Do not
        // race that action by unbinding its services here; its Stopped event
        // now arrives well within the two-second gate and drives the single
        // release path from update().
        record_trace(session, AssistantTraceComponent::Runtime,
                     AssistantTraceEvent::Release, state.state(),
                     AssistantTraceDetail::OfflineStop,
                     AssistantTraceResult::Accepted);
        return true;
    }
    if (stop_submitted.exchange(true)) {
        record_trace(session, AssistantTraceComponent::Runtime,
                     AssistantTraceEvent::ActionSubmit, state.state(),
                     AssistantTraceDetail::Stop, AssistantTraceResult::Duplicate);
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
    consumeUpdateNoticeReport(session);
    if (service_setup_pending && state.state() == AssistantRuntimeState::Starting &&
        GetSharedWifiService().staConnected()) {
        finishStart(session);
        if (state.state() == AssistantRuntimeState::Stopped) {
            return;
        }
    }
    if (service_setup_pending && state.state() == AssistantRuntimeState::Starting &&
        static_cast<uint32_t>(now_ms - wifi_connect_started_ms) >= wifi_connect_timeout_ms) {
        failStart(session, "Wi-Fi connection timed out; check Settings");
        return;
    }
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
        if (!submitSleep(session)) {
            failStart(session, "Initial sleep submission failed");
            return;
        }
    }

    if (state.state() == AssistantRuntimeState::Stopping &&
        static_cast<uint32_t>(now_ms - stop_started_ms) >= stop_timeout_ms) {
        if (state.markStopTimedOut(session) == TransitionResult::Accepted) {
            GetResourceSupervisor().block();
            stop_submitted.store(false);
            const auto resources = GetResourceSupervisor().snapshot();
            record_trace(session, AssistantTraceComponent::Runtime,
                         AssistantTraceEvent::StopTimeout, state.state(),
                         AssistantTraceDetail::Stop, AssistantTraceResult::TimedOut,
                         static_cast<int32_t>(now_ms - stop_started_ms),
                         static_cast<int32_t>(bindings.size()),
                         static_cast<int32_t>(subscriptionCount()),
                         static_cast<int32_t>(resources.playback_owner));
            mailbox.publish({session, AssistantEventKind::Error,
                             "Stop timed out; resources remain blocked"});
        }
    }
}

bool AssistantRuntime::Impl::finishStart(uint64_t session_id)
{
    service_setup_pending = false;
    try {
        log_memory_stage("wifi-connected-before-service-setup");
        if (!load_unified_wake_models()) {
            failStart(session_id, "Wake model unavailable");
            return false;
        }
        const auto capture_result =
            GetResourceSupervisor().acquireCapture(RuntimeOwner::Assistant);
        record_trace(session_id, AssistantTraceComponent::Resource,
                     AssistantTraceEvent::ResourceDecision, state.state(),
                     AssistantTraceDetail::Capture,
                     capture_result == AcquireResult::Granted
                         ? AssistantTraceResult::Accepted
                         : AssistantTraceResult::Busy);
        if (capture_result != AcquireResult::Granted) {
            failStart(session_id, "Microphone busy");
            return false;
        }
        capture_owned = true;
        const auto playback_result =
            GetResourceSupervisor().acquirePlayback(RuntimeOwner::Assistant);
        record_trace(session_id, AssistantTraceComponent::Resource,
                     AssistantTraceEvent::ResourceDecision, state.state(),
                     AssistantTraceDetail::Playback,
                     playback_result == AcquireResult::Granted
                         ? AssistantTraceResult::Accepted
                         : AssistantTraceResult::Busy);
        if (playback_result != AcquireResult::Granted) {
            failStart(session_id, "Speaker busy");
            return false;
        }
        playback_owned = true;
        if (!configurePlaybackBeforeStart() || !bindServices(session_id) ||
            !configureAudioAndAgent() || !subscribe(session_id)) {
            failStart(session_id, "Service setup failed");
            return false;
        }
        log_memory_stage("after-service-setup");
        if (!submitActivate(session_id)) {
            failStart(session_id, "Activate submission failed");
            return false;
        }
        mailbox.publish({session_id, AssistantEventKind::State, "Activating"});
        return true;
    } catch (const std::exception& error) {
        ESP_LOGE(tag, "exception during deferred Assistant setup: %s", error.what());
    } catch (...) {
        ESP_LOGE(tag, "unknown exception during deferred Assistant setup");
    }
    log_memory_stage("setup-exception");
    failStart(session_id, "Setup failed");
    return false;
}

bool AssistantRuntime::Impl::configurePlaybackBeforeStart()
{
    AudioHelper::PlaybackConfig playback_config{};
    playback_config.player_task.stack_in_ext = true;
    auto result = AudioHelper::call_function_sync(
        AudioHelper::FunctionId::SetPlaybackConfig,
        BROOKESIA_DESCRIBE_TO_JSON(playback_config).as_object());
    if (!result) {
        ESP_LOGE(tag, "failed to configure playback task: %s", result.error().c_str());
        return false;
    }
    ESP_LOGI(tag, "configured playback task stack in PSRAM");
    return true;
}

bool AssistantRuntime::Impl::bindServices(uint64_t session_id)
{
    if (callback_session.load() != session_id) {
        return false;
    }
    auto& manager = esp_brookesia::service::ServiceManager::get_instance();
    const std::array<std::string_view, 2> names = {
        NVSHelper::get_name(), ManagerHelper::get_name()};
    bindings.clear();
    for (const auto name : names) {
        auto binding = manager.bind(std::string(name));
        if (!binding.is_valid()) {
            ESP_LOGE(tag, "required service is unavailable: %.*s", static_cast<int>(name.size()),
                     name.data());
            bindings.clear();
            return false;
        }
        bindings.push_back(std::move(binding));
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
    // Brookesia initializes its desired volume to zero. Setting zero can therefore
    // be treated as a no-op before the hardware path has actually been muted.
    // Drive the independent mute state as well so HAL volume zero is unambiguous.
    auto mute_result = AudioHelper::call_function_sync(
        AudioHelper::FunctionId::SetMute, volume <= 0);
    if (!mute_result) {
        ESP_LOGE(tag, "failed to map current mute state: %s", mute_result.error().c_str());
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
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::Callback, state.state(),
                             AssistantTraceDetail::StaleSession,
                             AssistantTraceResult::Rejected);
                return;
            }
            uint32_t bits = unexpected ? static_cast<uint32_t>(UnexpectedLifecycle) : 0U;
            AssistantTraceDetail detail = AssistantTraceDetail::Unexpected;
            if (event == event_name(ManagerHelper::GeneralEvent::Activated)) {
                bits |= Activated;
                detail = AssistantTraceDetail::Activate;
            } else if (event == event_name(ManagerHelper::GeneralEvent::Started)) {
                bits |= Started;
                detail = AssistantTraceDetail::Running;
            } else if (event == event_name(ManagerHelper::GeneralEvent::Stopped)) {
                bits |= Stopped;
                detail = AssistantTraceDetail::Stopped;
            }
            if (bits != 0) {
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::Callback, state.state(), detail,
                             unexpected ? AssistantTraceResult::Rejected
                                        : AssistantTraceResult::Accepted);
                control_bits.fetch_or(bits);
            }
        });
    activation_code = XiaozhiHelper::subscribe_event(
        XiaozhiHelper::EventId::ActivationCodeReceived,
        [this, session_id](const std::string& source, const std::string& code) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    xiaozhi_event_name(XiaozhiHelper::EventId::ActivationCodeReceived))) {
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::Conversation, state.state(),
                             AssistantTraceDetail::Activate, AssistantTraceResult::Accepted,
                             static_cast<int32_t>(code.size()));
                mailbox.publish({session_id, AssistantEventKind::ActivationCode, code});
            }
        });
    user_text = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::UserSpeakingTextGot,
        [this, session_id](const std::string& source, const std::string& text) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::UserSpeakingTextGot))) {
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::Conversation, state.state(),
                             AssistantTraceDetail::Start, AssistantTraceResult::Accepted,
                             static_cast<int32_t>(text.size()));
                mailbox.publish({session_id, AssistantEventKind::UserText, text});
            }
        });
    agent_text = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::AgentSpeakingTextGot,
        [this, session_id](const std::string& source, const std::string& text) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::AgentSpeakingTextGot))) {
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::Conversation, state.state(),
                             AssistantTraceDetail::Running, AssistantTraceResult::Accepted,
                             static_cast<int32_t>(text.size()));
                mailbox.publish({session_id, AssistantEventKind::AgentText, text});
            }
        });
    listening_status = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::ListeningStatusChanged,
        [this, session_id](const std::string& source, bool listening) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::ListeningStatusChanged))) {
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::Conversation, state.state(),
                             AssistantTraceDetail::Listening, AssistantTraceResult::Accepted,
                             listening ? 1 : 0);
                mailbox.publish({session_id, AssistantEventKind::ListeningStatus, {}, listening});
            }
        });
    speaking_status = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::SpeakingStatusChanged,
        [this, session_id](const std::string& source, bool speaking) {
            if (callbackIsCurrentXiaozhi(
                    session_id, source,
                    manager_event_name(ManagerHelper::EventId::SpeakingStatusChanged))) {
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::Conversation, state.state(),
                             AssistantTraceDetail::Speaking, AssistantTraceResult::Accepted,
                             speaking ? 1 : 0);
                mailbox.publish({session_id, AssistantEventKind::SpeakingStatus, {}, speaking});
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
           agent_text.connected() && listening_status.connected() &&
           speaking_status.connected() && emote.connected();
}

bool AssistantRuntime::Impl::submitAction(uint64_t session_id,
                                          ManagerHelper::GeneralAction action,
                                          uint32_t failure_bit)
{
    const std::string name = action_name(action);
    const AssistantTraceDetail detail = action_detail(action);
    try {
        const bool submitted = ManagerHelper::call_function_async(
            ManagerHelper::FunctionId::TriggerGeneralAction, name,
            [this, session_id, failure_bit, detail](FunctionResult&& result) {
                if (callback_session.load() != session_id || result.success) {
                    return;
                }
                record_trace(session_id, AssistantTraceComponent::Callback,
                             AssistantTraceEvent::ActionSubmit, state.state(), detail,
                             AssistantTraceResult::Failed);
                control_bits.fetch_or(failure_bit);
            });
        record_trace(session_id, AssistantTraceComponent::Runtime,
                     AssistantTraceEvent::ActionSubmit, state.state(), detail,
                     submitted ? AssistantTraceResult::Accepted
                               : AssistantTraceResult::Rejected);
        if (!submitted) {
            ESP_LOGE(tag, "failed to submit %s request", name.c_str());
        }
        return submitted;
    } catch (const std::exception& error) {
        ESP_LOGE(tag, "exception submitting %s request: %s", name.c_str(), error.what());
    } catch (...) {
        ESP_LOGE(tag, "unknown exception submitting %s request", name.c_str());
    }
    return false;
}

bool AssistantRuntime::Impl::submitActivate(uint64_t session_id)
{
    return submitAction(session_id, ManagerHelper::GeneralAction::Activate, StartAsyncFailed);
}

bool AssistantRuntime::Impl::submitStart(uint64_t session_id)
{
    return submitAction(session_id, ManagerHelper::GeneralAction::Start, StartAsyncFailed);
}

bool AssistantRuntime::Impl::submitSleep(uint64_t session_id)
{
    return submitAction(session_id, ManagerHelper::GeneralAction::Sleep, StartAsyncFailed);
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
    listening_status.disconnect();
    speaking_status.disconnect();
    emote.disconnect();
}

void AssistantRuntime::Impl::restoreUpdateNotice(uint64_t session_id)
{
    const auto action = update_notice.restore(update_notice_store.load());
    applyUpdateStorageAction(action);
    publishUpdateNotice(session_id);
}

void AssistantRuntime::Impl::consumeUpdateNoticeReport(uint64_t session_id)
{
    const auto report = GetAssistantUpdateNoticeBridge().consume();
    if (!report) {
        return;
    }
    if (report->session_id != state.sessionId() || report->session_id != session_id) {
        ESP_LOGW(tag, "discarded stale update notice report for session %" PRIu64,
                 report->session_id);
        return;
    }

    if (!NormalizeAssistantFirmwareVersion(report->version)) {
        ESP_LOGW(tag, "ignored invalid remote firmware version");
    }
    const auto action = update_notice.observe(report->version);
    applyUpdateStorageAction(action);
    publishUpdateNotice(session_id);
}

void AssistantRuntime::Impl::applyUpdateStorageAction(AssistantUpdateStorageAction action)
{
    switch (action) {
        case AssistantUpdateStorageAction::None:
            return;
        case AssistantUpdateStorageAction::Save:
            update_notice_store.save(update_notice.pendingVersion());
            return;
        case AssistantUpdateStorageAction::Clear:
            update_notice_store.clear();
            return;
    }
}

void AssistantRuntime::Impl::publishUpdateNotice(uint64_t session_id)
{
    if (update_notice.hasPending()) {
        mailbox.publish(
            {session_id, AssistantEventKind::UpdateAvailable, update_notice.pendingVersion()});
    }
}

void AssistantRuntime::Impl::releaseSession(uint64_t session_id)
{
    GetAssistantUpdateNoticeBridge().endSession(session_id);
    if (session_id != state.sessionId()) {
        record_trace(session_id, AssistantTraceComponent::Runtime,
                     AssistantTraceEvent::Release, state.state(),
                     AssistantTraceDetail::StaleSession, AssistantTraceResult::Rejected);
        return;
    }
    if (state.state() == AssistantRuntimeState::Starting ||
        state.state() == AssistantRuntimeState::Running ||
        state.state() == AssistantRuntimeState::Failed) {
        state.requestStop();
    }

    record_trace(session_id, AssistantTraceComponent::Runtime,
                 AssistantTraceEvent::Release, state.state(),
                 AssistantTraceDetail::AllResources, AssistantTraceResult::Accepted,
                 static_cast<int32_t>(bindings.size()),
                 static_cast<int32_t>(subscriptionCount()),
                 playback_owned ? 1 : 0, capture_owned ? 1 : 0);
    disconnectSubscriptions();
    service_setup_pending = false;
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
    const auto resources = GetResourceSupervisor().snapshot();
    record_trace(session_id, AssistantTraceComponent::Runtime,
                 AssistantTraceEvent::Release, state.state(),
                 AssistantTraceDetail::AllResources, AssistantTraceResult::Ok,
                 static_cast<int32_t>(bindings.size()),
                 static_cast<int32_t>(subscriptionCount()),
                 static_cast<int32_t>(resources.capture_owner),
                 static_cast<int32_t>(resources.playback_owner));
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
           static_cast<std::size_t>(listening_status.connected()) +
           static_cast<std::size_t>(speaking_status.connected()) +
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
