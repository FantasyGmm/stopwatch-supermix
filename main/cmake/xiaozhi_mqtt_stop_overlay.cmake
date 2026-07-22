function(apply_xiaozhi_mqtt_stop_overlay)
    idf_component_get_property(xiaozhi_lib
        espressif__esp_xiaozhi COMPONENT_LIB)
    idf_component_get_property(xiaozhi_dir
        espressif__esp_xiaozhi COMPONENT_DIR)

    set(upstream_mqtt "${xiaozhi_dir}/src/esp_xiaozhi_mqtt.c")
    set(original_network_config [=[        .session = {
            .keepalive = 90,
        },]=])
    set(fast_stop_network_config [=[        .session = {
            .keepalive = 90,
        },
        .network = {
            // esp_mqtt_client_stop() waits for the reconnect poll to finish. The
            // default 10 s interval can hold Assistant teardown for up to 5 s
            // after Wi-Fi drops, exceeding the two-second close gate.
            .reconnect_timeout_ms = 500,
        },]=])

    file(READ "${upstream_mqtt}" mqtt_source)
    string(FIND "${mqtt_source}" "${original_network_config}" network_position)
    if(network_position EQUAL -1)
        message(FATAL_ERROR
            "XiaoZhi MQTT configuration changed; review the fast-stop overlay")
    endif()
    string(REPLACE "${original_network_config}" "${fast_stop_network_config}"
        mqtt_source "${mqtt_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/xiaozhi_mqtt_stop")
    set(generated_mqtt "${generated_dir}/esp_xiaozhi_mqtt.c")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_mqtt}" "${mqtt_source}")

    get_target_property(xiaozhi_sources ${xiaozhi_lib} SOURCES)
    list(REMOVE_ITEM xiaozhi_sources "${upstream_mqtt}")
    set_property(TARGET ${xiaozhi_lib} PROPERTY SOURCES "${xiaozhi_sources}")
    target_sources(${xiaozhi_lib} PRIVATE "${generated_mqtt}")
endfunction()
