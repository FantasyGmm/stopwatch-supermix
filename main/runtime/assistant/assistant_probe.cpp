/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "assistant_probe.h"
#include "hal/hal.h"
#include "runtime/service_runtime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <inttypes.h>
#include <memory>
#include <string>
#include <vector>

#include <boost/json.hpp>
#include <brookesia/agent_helper/manager.hpp>
#include <brookesia/agent_helper/xiaozhi.hpp>
#include <brookesia/lib_utils/describe_helpers.hpp>
#include <brookesia/service_helper/audio.hpp>
#include <brookesia/service_manager.hpp>
#include <esp_log.h>
#include <esp_partition.h>
#include <model_path.h>

namespace {
using AudioHelper = esp_brookesia::service::helper::Audio;
using ManagerHelper = esp_brookesia::agent::helper::Manager;
using XiaozhiHelper = esp_brookesia::agent::helper::XiaoZhi;
using SignalConnection = esp_brookesia::service::EventRegistry::SignalConnection;
using FunctionResult = esp_brookesia::service::FunctionResult;

constexpr const char* tag = "AssistantProbe";
constexpr const char* assets_partition_label = "assets";
constexpr const char* srmodels_asset_name = "srmodels.bin";

struct __attribute__((packed)) AssetTableEntry {
    char name[32];
    uint32_t size;
    uint32_t offset;
    uint16_t width;
    uint16_t height;
};

static_assert(sizeof(AssetTableEntry) == 44);

struct ProbeState {
    std::atomic<bool> active{false};
    std::atomic<bool> stopped{true};
    std::atomic<bool> stop_requested{false};
    std::vector<std::shared_ptr<esp_brookesia::service::ServiceBase>> bindings;
    SignalConnection general_event;
    SignalConnection activation_code;
    SignalConnection user_text;
    SignalConnection agent_text;
    SignalConnection emote;
};

ProbeState probe;
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

void disconnect_subscriptions()
{
    probe.general_event.disconnect();
    probe.activation_code.disconnect();
    probe.user_text.disconnect();
    probe.agent_text.disconnect();
    probe.emote.disconnect();
    probe.bindings.clear();
}

void handle_async_result(const char* operation, FunctionResult&& result)
{
    if (result.success) {
        ESP_LOGI(tag, "%s request accepted", operation);
    } else {
        ESP_LOGE(tag, "%s request failed: %s", operation, result.error_message.c_str());
    }
}

bool trigger_action_async(ManagerHelper::GeneralAction action)
{
    const std::string name = action_name(action);
    const bool submitted = ManagerHelper::call_function_async(
        ManagerHelper::FunctionId::TriggerGeneralAction, name,
        [name](FunctionResult&& result) { handle_async_result(name.c_str(), std::move(result)); });
    if (!submitted) {
        ESP_LOGE(tag, "failed to submit %s request", name.c_str());
    }
    return submitted;
}

bool bind_required_services()
{
    auto& manager = esp_brookesia::service::ServiceManager::get_instance();
    const std::array<std::string_view, 3> names = {
        AudioHelper::get_name(), ManagerHelper::get_name(), XiaozhiHelper::get_name()};
    probe.bindings.clear();
    for (const auto name : names) {
        auto service = manager.get_service(std::string(name));
        if (service == nullptr) {
            ESP_LOGE(tag, "required service is unavailable: %.*s", static_cast<int>(name.size()),
                     name.data());
            probe.bindings.clear();
            return false;
        }
        probe.bindings.push_back(std::move(service));
    }
    return true;
}

bool configure_audio_model_and_volume()
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
    return true;
}

bool configure_agent_identity()
{
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
    return true;
}

bool subscribe_probe_events()
{
    probe.general_event = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::GeneralEventHappened,
        [](const std::string&, const std::string& event, bool unexpected) {
            ESP_LOGI(tag, "agent event=%s unexpected=%s", event.c_str(), unexpected ? "yes" : "no");
            if (unexpected) {
                ESP_LOGE(tag, "unexpected agent lifecycle event: %s", event.c_str());
            }
            if (event == event_name(ManagerHelper::GeneralEvent::Activated)) {
                trigger_action_async(ManagerHelper::GeneralAction::Start);
            } else if (event == event_name(ManagerHelper::GeneralEvent::Started)) {
                ESP_LOGI(tag, "connection state=connected");
            } else if (event == event_name(ManagerHelper::GeneralEvent::Stopped)) {
                ESP_LOGI(tag, "connection state=disconnected; stopped acknowledged");
                probe.stopped.store(true);
                probe.active.store(false);
                probe.stop_requested.store(false);
                disconnect_subscriptions();
            }
        });
    probe.activation_code = XiaozhiHelper::subscribe_event(
        XiaozhiHelper::EventId::ActivationCodeReceived,
        [](const std::string&, const std::string& code) {
            ESP_LOGI(tag, "activation code=%s", code.c_str());
        });
    probe.user_text = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::UserSpeakingTextGot,
        [](const std::string&, const std::string& text) {
            ESP_LOGI(tag, "user text=%s", text.c_str());
        });
    probe.agent_text = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::AgentSpeakingTextGot,
        [](const std::string&, const std::string& text) {
            ESP_LOGI(tag, "assistant text=%s", text.c_str());
        });
    probe.emote = ManagerHelper::subscribe_event(
        ManagerHelper::EventId::EmoteGot,
        [](const std::string&, const std::string& emote) {
            ESP_LOGI(tag, "assistant emotion=%s", emote.c_str());
        });

    return probe.general_event.connected() && probe.activation_code.connected() &&
           probe.user_text.connected() && probe.agent_text.connected() && probe.emote.connected();
}
} // namespace

bool StartAssistantCompatibilityProbe()
{
    if (probe.active.exchange(true)) {
        return true;
    }
    probe.stopped.store(false);
    probe.stop_requested.store(false);

    if (!GetServiceRuntime().isStarted() || !load_unified_wake_models() ||
        !bind_required_services() || !configure_audio_model_and_volume() ||
        !configure_agent_identity() || !subscribe_probe_events() ||
        !trigger_action_async(ManagerHelper::GeneralAction::Activate)) {
        ESP_LOGE(tag, "compatibility probe failed to start");
        disconnect_subscriptions();
        probe.active.store(false);
        probe.stopped.store(true);
        return false;
    }

    ESP_LOGI(tag, "compatibility probe started");
    return true;
}

bool StopAssistantCompatibilityProbe()
{
    if (!probe.active.load()) {
        return probe.stopped.load();
    }
    if (probe.stop_requested.exchange(true)) {
        return true;
    }
    const bool submitted = trigger_action_async(ManagerHelper::GeneralAction::Stop);
    if (!submitted) {
        probe.stop_requested.store(false);
    }
    return submitted;
}

bool AssistantCompatibilityProbeStopped()
{
    return probe.stopped.load();
}
