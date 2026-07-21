function(apply_esp_wifi_connect_priority_overlay)
    idf_component_get_property(wifi_connect_lib
        78__esp-wifi-connect COMPONENT_LIB)
    idf_component_get_property(wifi_connect_dir
        78__esp-wifi-connect COMPONENT_DIR)

    set(upstream_wifi_station "${wifi_connect_dir}/wifi_station.cc")
    set(original_ssid_include [=[#include "ssid_manager.h"]=])
    set(priority_include [=[#include "ssid_manager.h"
#include "runtime/services/wifi_candidate_priority.h"]=])
    set(original_ordering [=[    // sort by rssi descending
    std::sort(ap_records, ap_records + ap_num, [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) {
        return a.rssi > b.rssi;
    });

    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();]=])
    set(priority_ordering [=[    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    std::sort(ap_records, ap_records + ap_num,
              [&ssid_list](const wifi_ap_record_t& left, const wifi_ap_record_t& right) {
                  return stopwatch::wifi::savedCandidatePriorityLess(
                      reinterpret_cast<const char*>(left.ssid), left.rssi,
                      reinterpret_cast<const char*>(right.ssid), right.rssi, ssid_list,
                      [](const SsidItem& saved) { return std::string_view(saved.ssid); });
              });]=])
    string(CONCAT original_exhausted
        "        ESP_LOGI(TAG, \"No more AP to connect, next scan in %d seconds\","
        " \n"
        "                 this_->scan_current_interval_microseconds_ / 1000 / 1000);")
    set(clear_exhausted [=[        this_->ssid_.clear();
        this_->ip_address_.clear();
        ESP_LOGI(TAG, "No more AP to connect, next scan in %d seconds",
                 this_->scan_current_interval_microseconds_ / 1000 / 1000);]=])
    set(original_stop_reset [=[    // Reset was_connected_ flag to prevent stale state from affecting subsequent sessions
    was_connected_ = false;

    // Clear connected bit]=])
    set(complete_stop_reset [=[    // Reset connection state before the next saved-priority scan.
    was_connected_ = false;
    reconnect_count_ = 0;
    connect_queue_.clear();
    ssid_.clear();
    password_.clear();
    ip_address_.clear();

    // Clear connected bit]=])

    file(READ "${upstream_wifi_station}" wifi_station_source)
    foreach(required_text IN ITEMS
            "${original_ssid_include}" "${original_ordering}"
            "${original_exhausted}" "${original_stop_reset}")
        string(FIND "${wifi_station_source}" "${required_text}" required_position)
        if(required_position EQUAL -1)
            message(FATAL_ERROR
                "ESP WiFi Connect station behavior changed; review the saved-priority overlay")
        endif()
    endforeach()
    string(REPLACE "${original_ssid_include}" "${priority_include}"
        wifi_station_source "${wifi_station_source}")
    string(REPLACE "${original_ordering}" "${priority_ordering}"
        wifi_station_source "${wifi_station_source}")
    string(REPLACE "${original_exhausted}" "${clear_exhausted}"
        wifi_station_source "${wifi_station_source}")
    string(REPLACE "${original_stop_reset}" "${complete_stop_reset}"
        wifi_station_source "${wifi_station_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/esp_wifi_connect_priority")
    set(generated_wifi_station "${generated_dir}/wifi_station.cc")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_wifi_station}" "${wifi_station_source}")

    get_target_property(wifi_connect_sources ${wifi_connect_lib} SOURCES)
    list(REMOVE_ITEM wifi_connect_sources "${upstream_wifi_station}")
    set_property(TARGET ${wifi_connect_lib} PROPERTY SOURCES
        "${wifi_connect_sources}")
    target_sources(${wifi_connect_lib} PRIVATE "${generated_wifi_station}")
    target_include_directories(${wifi_connect_lib} PRIVATE "${CMAKE_SOURCE_DIR}/main")
endfunction()
