# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(StopWatch-UserDemo)

include(${CMAKE_SOURCE_DIR}/main/cmake/brookesia_task_scheduler_overlay.cmake)
apply_brookesia_task_scheduler_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/brookesia_service_stop_overlay.cmake)
apply_brookesia_service_stop_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/brookesia_agent_manager_overlay.cmake)
apply_brookesia_agent_manager_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/brookesia_xiaozhi_mcp_ownership_overlay.cmake)
apply_brookesia_xiaozhi_mcp_ownership_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/xiaozhi_mqtt_stop_overlay.cmake)
apply_xiaozhi_mqtt_stop_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/audio_processor_light_overlay.cmake)
apply_audio_processor_light_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/brookesia_audio_light_overlay.cmake)
apply_brookesia_audio_light_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/esp_wifi_connect_security_overlay.cmake)
apply_esp_wifi_connect_security_overlay()

include(${CMAKE_SOURCE_DIR}/main/cmake/esp_wifi_connect_priority_overlay.cmake)
apply_esp_wifi_connect_priority_overlay()

idf_component_get_property(bt_lib bt COMPONENT_LIB)
if(bt_lib)
    target_compile_options(${bt_lib} PRIVATE -Wno-error=array-bounds)
endif()
