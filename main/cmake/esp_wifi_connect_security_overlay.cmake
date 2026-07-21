function(apply_esp_wifi_connect_security_overlay)
    idf_component_get_property(wifi_connect_lib
        78__esp-wifi-connect COMPONENT_LIB)
    idf_component_get_property(wifi_connect_dir
        78__esp-wifi-connect COMPONENT_DIR)

    set(upstream_wifi_configuration
        "${wifi_connect_dir}/wifi_configuration_ap.cc")
    set(smartconfig_password_log [=[            ESP_LOGI(TAG, "SmartConfig SSID: %s, Password: %s", ssid, password);]=])
    set(smartconfig_credentials_log [=[            ESP_LOGI(TAG, "SmartConfig credentials received");]=])
    set(copy_password [=[            memcpy(password, evt->password, sizeof(evt->password));]=])
    set(save_credentials [=[            self->Save(ssid, password);]=])

    file(READ "${upstream_wifi_configuration}" wifi_configuration_source)
    foreach(required_text IN ITEMS
            "${smartconfig_password_log}" "${copy_password}" "${save_credentials}")
        string(FIND "${wifi_configuration_source}" "${required_text}" required_position)
        if(required_position EQUAL -1)
            message(FATAL_ERROR
                "ESP WiFi Connect SmartConfig changed; review the credential security overlay")
        endif()
    endforeach()
    string(REPLACE "${smartconfig_password_log}" "${smartconfig_credentials_log}"
        wifi_configuration_source "${wifi_configuration_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/esp_wifi_connect_security")
    set(generated_wifi_configuration "${generated_dir}/wifi_configuration_ap.cc")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_wifi_configuration}" "${wifi_configuration_source}")

    get_target_property(wifi_connect_sources ${wifi_connect_lib} SOURCES)
    list(REMOVE_ITEM wifi_connect_sources "${upstream_wifi_configuration}")
    set_property(TARGET ${wifi_connect_lib} PROPERTY SOURCES
        "${wifi_connect_sources}")
    target_sources(${wifi_connect_lib} PRIVATE "${generated_wifi_configuration}")
endfunction()
