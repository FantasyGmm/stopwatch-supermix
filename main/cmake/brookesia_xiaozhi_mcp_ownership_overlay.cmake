function(apply_brookesia_xiaozhi_mcp_ownership_overlay)
    idf_component_get_property(xiaozhi_agent_lib
        espressif__brookesia_agent_xiaozhi COMPONENT_LIB)
    idf_component_get_property(xiaozhi_agent_dir
        espressif__brookesia_agent_xiaozhi COMPONENT_DIR)

    set(upstream_xiaozhi_agent "${xiaozhi_agent_dir}/src/agent_xiaozhi.cpp")
    set(xiaozhi_info_include [=[#include "esp_xiaozhi_info.h"]=])
    set(xiaozhi_info_include_with_version_bridge [=[#include "esp_xiaozhi_info.h"

#include <stdint.h>

extern "C" char* stopwatch_duplicate_firmware_version();
extern "C" uint64_t stopwatch_capture_firmware_update_session_token();
extern "C" void stopwatch_report_remote_firmware_version(uint64_t session_token, const char* version);]=])
    set(unversioned_info_request [=[        esp_xiaozhi_chat_info_t info{};
        auto ret = esp_xiaozhi_chat_get_info(&info);
        BROOKESIA_CHECK_ESP_ERR_RETURN(
            ret, true, "Failed to get info, retry in %1% ms", BROOKESIA_AGENT_XIAOZHI_GET_HTTP_INFO_INTERVAL_MS
        );

        lib_utils::FunctionGuard free_info_guard([this, &info]() {
            BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
            esp_xiaozhi_chat_free_info(&info);
        });]=])
    set(versioned_info_request [=[        esp_xiaozhi_chat_info_t info{};
        const auto update_notice_session_token = stopwatch_capture_firmware_update_session_token();
        info.current_version = stopwatch_duplicate_firmware_version();
        auto ret = esp_xiaozhi_chat_get_info(&info);
        if (ret != ESP_OK) {
            esp_xiaozhi_chat_free_info(&info);
        }
        BROOKESIA_CHECK_ESP_ERR_RETURN(
            ret, true, "Failed to get info, retry in %1% ms", BROOKESIA_AGENT_XIAOZHI_GET_HTTP_INFO_INTERVAL_MS
        );

        lib_utils::FunctionGuard free_info_guard([this, &info]() {
            BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
            esp_xiaozhi_chat_free_info(&info);
        });

        if (ret == ESP_OK && info.firmware_version != nullptr) {
            stopwatch_report_remote_firmware_version(update_notice_session_token, info.firmware_version);
        }]=])
    set(borrowed_mcp_engine [=[    chat_config.mcp_engine = mcp_engine;
    auto ret_init = esp_xiaozhi_chat_init(&chat_config, &chat_handle_);]=])
    set(owned_mcp_engine [=[    chat_config.mcp_engine = mcp_engine;
    // The engine is created for this chat session. Transfer ownership so
    // esp_xiaozhi_chat_deinit() destroys it after stopping MCP runtime users.
    chat_config.owns_mcp_engine = true;
    auto ret_init = esp_xiaozhi_chat_init(&chat_config, &chat_handle_);]=])
    set(connected_auto_open [=[            if (open_audio_channel_task_ != 0) {
                BROOKESIA_LOGW("Open audio channel task is already running, skip");
                return;
            }

            // Open audio channel
            // Delay to ensure the audio channel is opened after the agent is started
            // Otherwise, the MCP tools may not be available in first chat
            auto open_audio_channel_func = [this]() {
                BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

                // Skip if XiaoZhi is stopped
                if (!is_xiaozhi_started()) {
                    BROOKESIA_LOGW("XiaoZhi is not started, skip");
                    return;
                }

                open_audio_channel_task_ = 0;

                auto get_format_str = [](AudioHelper::CodecFormat format) {
                    switch (format) {
                    case AudioHelper::CodecFormat::OPUS:
                        return "opus";
                    case AudioHelper::CodecFormat::G711A:
                        return "g711a";
                    default:
                        return "pcm";
                    }
                };
                auto &audio_config = get_audio_config();
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
                esp_xiaozhi_chat_audio_t chat_audio = {
                    .format = get_format_str(audio_config.decoder.type),
                    .sample_rate = static_cast<int>(audio_config.decoder.general.sample_rate),
                    .channels = static_cast<int>(audio_config.decoder.general.channels),
                    .frame_duration = static_cast<int>(audio_config.decoder.general.frame_duration),
                };
#pragma GCC diagnostic pop
                auto ret = esp_xiaozhi_chat_open_audio_channel(chat_handle_, &chat_audio, nullptr, 0);
                BROOKESIA_CHECK_ESP_ERR_EXECUTE(ret, {
                    trigger_general_event(GeneralEvent::Stopped);
                }, {
                    BROOKESIA_LOGE("Failed to open audio channel");
                });
            };
            auto scheduler = get_task_scheduler();
            BROOKESIA_CHECK_NULL_EXIT(scheduler, "Scheduler is not available");
            auto group = get_state_task_group();
            auto result = scheduler->post_delayed(
                              open_audio_channel_func, OPEN_AUDIO_CHANNEL_DELAY_MS, &open_audio_channel_task_, group
                          );
            BROOKESIA_CHECK_FALSE_EXIT(result, "Failed to post task function");]=])
    set(wait_for_afe_wake [=[            // Keep the transport connected without creating a server audio session.
            // The runtime enters Sleep after Started, so AFE WakeStart is the only
            // path that invokes on_wakeup() and opens the audio channel.]=])

    file(READ "${upstream_xiaozhi_agent}" xiaozhi_agent_source)
    string(FIND "${xiaozhi_agent_source}" "${xiaozhi_info_include}"
        xiaozhi_info_include_position)
    if(xiaozhi_info_include_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia XiaoZhi includes changed; review the version bridge overlay")
    endif()
    string(REPLACE "${xiaozhi_info_include}" "${xiaozhi_info_include_with_version_bridge}"
        xiaozhi_agent_source "${xiaozhi_agent_source}")
    string(FIND "${xiaozhi_agent_source}" "${unversioned_info_request}"
        info_request_position)
    if(info_request_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia XiaoZhi info request changed; review the version bridge overlay")
    endif()
    string(REPLACE "${unversioned_info_request}" "${versioned_info_request}"
        xiaozhi_agent_source "${xiaozhi_agent_source}")
    string(FIND "${xiaozhi_agent_source}" "${borrowed_mcp_engine}"
        ownership_position)
    if(ownership_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia XiaoZhi MCP setup changed; review the ownership overlay")
    endif()
    string(REPLACE "${borrowed_mcp_engine}" "${owned_mcp_engine}"
        xiaozhi_agent_source "${xiaozhi_agent_source}")
    string(FIND "${xiaozhi_agent_source}" "${connected_auto_open}"
        connected_auto_open_position)
    if(connected_auto_open_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia XiaoZhi connected flow changed; review the wake gate overlay")
    endif()
    string(REPLACE "${connected_auto_open}" "${wait_for_afe_wake}"
        xiaozhi_agent_source "${xiaozhi_agent_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/brookesia_xiaozhi_mcp_ownership")
    set(generated_xiaozhi_agent "${generated_dir}/agent_xiaozhi.cpp")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_xiaozhi_agent}" "${xiaozhi_agent_source}")

    get_target_property(xiaozhi_agent_sources ${xiaozhi_agent_lib} SOURCES)
    list(REMOVE_ITEM xiaozhi_agent_sources "${upstream_xiaozhi_agent}")
    set_property(TARGET ${xiaozhi_agent_lib} PROPERTY SOURCES
        "${xiaozhi_agent_sources}")
    target_sources(${xiaozhi_agent_lib} PRIVATE "${generated_xiaozhi_agent}")
endfunction()
