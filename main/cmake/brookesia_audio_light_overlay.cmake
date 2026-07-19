function(apply_brookesia_audio_light_overlay)
    idf_component_get_property(brookesia_audio_lib
        espressif__brookesia_service_audio COMPONENT_LIB)
    idf_component_get_property(brookesia_audio_dir
        espressif__brookesia_service_audio COMPONENT_DIR)

    set(upstream_service "${brookesia_audio_dir}/src/service_audio.cpp")
    set(upstream_converter "${brookesia_audio_dir}/src/type_converter.cpp")

    set(original_afe_convert [=[#if !AV_PROCESSOR_AFE_USE_CUSTOM
    av_processor_afe_config_t result = DEFAULT_AV_PROCESSOR_AFE_CONFIG();
    auto &afe_config = result.mode.afe;
#else
    av_processor_afe_config_t result = DEFAULT_AV_PROCESSOR_CUSTOM_CONFIG();
    auto &afe_config = result.mode.custom;
#endif

    if (config.vad.has_value()) {
#if !AV_PROCESSOR_AFE_USE_CUSTOM
        afe_config.algo_mask |= AV_PROCESSOR_AFE_FLAG_AEC_ENABLE |
                                AV_PROCESSOR_AFE_FLAG_NS_ENABLE |
                                AV_PROCESSOR_AFE_FLAG_VAD_ENABLE |
                                AV_PROCESSOR_AFE_FLAG_AGC_ENABLE;
#else
        BROOKESIA_LOGW("VAD is not supported on this target");
#endif
    }

    if (config.wakenet.has_value()) {
        afe_config.algo_mask |= AV_PROCESSOR_CUSTOM_FLAG_WAKENET_ENABLE;
#if !AV_PROCESSOR_AFE_USE_CUSTOM
        const auto &wakenet = config.wakenet.value();
        afe_config.wakeup_time_ms = wakenet.start_timeout_ms;
        afe_config.wakeup_end_time_ms = wakenet.end_timeout_ms;
        // Store strings in persistent storage
        model_storage = wakenet.model_partition_label;
        mn_language_storage = wakenet.mn_language;
        afe_config.mn_language = mn_language_storage.c_str();
        result.model = model_storage.c_str();
#endif
    }]=])
    set(light_afe_convert [=[#if CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT || AV_PROCESSOR_AFE_USE_CUSTOM
    av_processor_afe_config_t result = DEFAULT_AV_PROCESSOR_CUSTOM_CONFIG();
    auto &afe_config = result.mode.custom;
#else
    av_processor_afe_config_t result = DEFAULT_AV_PROCESSOR_AFE_CONFIG();
    auto &afe_config = result.mode.afe;
#endif

    if (config.vad.has_value()) {
#if !CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT && !AV_PROCESSOR_AFE_USE_CUSTOM
        afe_config.algo_mask |= AV_PROCESSOR_AFE_FLAG_AEC_ENABLE |
                                AV_PROCESSOR_AFE_FLAG_NS_ENABLE |
                                AV_PROCESSOR_AFE_FLAG_VAD_ENABLE |
                                AV_PROCESSOR_AFE_FLAG_AGC_ENABLE;
#else
        BROOKESIA_LOGW("VAD is not supported by the custom audio frontend");
#endif
    }

    if (config.wakenet.has_value()) {
        afe_config.algo_mask |= AV_PROCESSOR_CUSTOM_FLAG_WAKENET_ENABLE;
        const auto &wakenet = config.wakenet.value();
        // Both frontends need the real partition label; the light implementation
        // cannot fall back to the project's assets partition from "model".
        model_storage = wakenet.model_partition_label;
        result.model = model_storage.c_str();
#if !CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT && !AV_PROCESSOR_AFE_USE_CUSTOM
        afe_config.wakeup_time_ms = wakenet.start_timeout_ms;
        afe_config.wakeup_end_time_ms = wakenet.end_timeout_ms;
        mn_language_storage = wakenet.mn_language;
        afe_config.mn_language = mn_language_storage.c_str();
#endif
    }]=])

    set(original_mixer_open [=[    auto playback_handle = TypeConverter::to_playback_handle(playback_handle_);
    BROOKESIA_CHECK_ESP_ERR_RETURN(
        audio_processor_mixer_open(playback_handle, feeder_handle), false, "Failed to open mixer"
    );
    lib_utils::FunctionGuard close_mixer_guard([this]() {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
        BROOKESIA_CHECK_ESP_ERR_EXECUTE(audio_processor_mixer_close(), {}, {
            BROOKESIA_LOGE("Failed to close mixer");
        });
    });]=])
    set(light_mixer_open [=[#if !CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT
    auto playback_handle = TypeConverter::to_playback_handle(playback_handle_);
    BROOKESIA_CHECK_ESP_ERR_RETURN(
        audio_processor_mixer_open(playback_handle, feeder_handle), false, "Failed to open mixer"
    );
    lib_utils::FunctionGuard close_mixer_guard([this]() {
        BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();
        BROOKESIA_CHECK_ESP_ERR_EXECUTE(audio_processor_mixer_close(), {}, {
            BROOKESIA_LOGE("Failed to close mixer");
        });
    });
#else
    // The light implementation writes feeder PCM directly to the board output.
#endif]=])
    set(original_mixer_release [=[    close_feeder_guard.release();
    close_mixer_guard.release();]=])
    set(light_mixer_release [=[    close_feeder_guard.release();
#if !CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT
    close_mixer_guard.release();
#endif]=])
    set(original_mixer_close [=[    BROOKESIA_CHECK_ESP_ERR_EXECUTE(audio_processor_mixer_close(), {}, {
        BROOKESIA_LOGE("Failed to close mixer");
    });
    is_decoder_started_ = false;]=])
    set(light_mixer_close [=[#if !CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT
    BROOKESIA_CHECK_ESP_ERR_EXECUTE(audio_processor_mixer_close(), {}, {
        BROOKESIA_LOGE("Failed to close mixer");
    });
#endif
    is_decoder_started_ = false;]=])
    set(original_event_cast [=[    auto afe_evt = static_cast<esp_gmf_afe_evt_t *>(event);
    BROOKESIA_CHECK_NULL_EXIT(afe_evt, "AFE event is null");]=])
    set(light_event_cast [=[#if CONFIG_AUDIO_PROCESSOR_IMPL_LIGHT
    {
        auto *wn_evt = static_cast<audio_recorder_wn_event_t *>(event);
        BROOKESIA_CHECK_NULL_EXIT(wn_evt, "WakeNet event is null");
        BROOKESIA_LOGD("WakeNet detected on channel: %1%", wn_evt->trigger_ch);
        auto result = publish_event(BROOKESIA_DESCRIBE_TO_STR(Helper::EventId::AFE_EventHappened), {
            BROOKESIA_DESCRIBE_TO_STR(Helper::AFE_Event::WakeStart)
        });
        BROOKESIA_CHECK_FALSE_EXIT(result, "Failed to publish WakeNet event");
    }
    return;
#endif
    auto afe_evt = static_cast<esp_gmf_afe_evt_t *>(event);
    BROOKESIA_CHECK_NULL_EXIT(afe_evt, "AFE event is null");]=])

    file(READ "${upstream_converter}" converter_source)
    string(FIND "${converter_source}" "${original_afe_convert}" converter_position)
    if(converter_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia audio converter changed; review the light frontend overlay")
    endif()
    string(REPLACE "${original_afe_convert}" "${light_afe_convert}"
        converter_source "${converter_source}")

    file(READ "${upstream_service}" service_source)
    foreach(required_text IN ITEMS
            "${original_mixer_open}" "${original_mixer_release}"
            "${original_mixer_close}" "${original_event_cast}")
        string(FIND "${service_source}" "${required_text}" required_position)
        if(required_position EQUAL -1)
            message(FATAL_ERROR
                "Brookesia audio service changed; review the light processor overlay")
        endif()
    endforeach()
    string(REPLACE "${original_mixer_open}" "${light_mixer_open}"
        service_source "${service_source}")
    string(REPLACE "${original_mixer_release}" "${light_mixer_release}"
        service_source "${service_source}")
    string(REPLACE "${original_mixer_close}" "${light_mixer_close}"
        service_source "${service_source}")
    string(REPLACE "${original_event_cast}" "${light_event_cast}"
        service_source "${service_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/brookesia_audio_light")
    set(generated_service "${generated_dir}/service_audio.cpp")
    set(generated_converter "${generated_dir}/type_converter.cpp")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_service}" "${service_source}")
    file(WRITE "${generated_converter}" "${converter_source}")

    get_target_property(service_audio_sources ${brookesia_audio_lib} SOURCES)
    list(REMOVE_ITEM service_audio_sources "${upstream_service}")
    set(type_converter_sources "${service_audio_sources}")
    list(REMOVE_ITEM type_converter_sources "${upstream_converter}")
    set_property(TARGET ${brookesia_audio_lib} PROPERTY SOURCES
        "${type_converter_sources}")
    target_sources(${brookesia_audio_lib} PRIVATE
        "${generated_service}" "${generated_converter}")
endfunction()
