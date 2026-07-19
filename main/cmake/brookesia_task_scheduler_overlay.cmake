function(apply_brookesia_task_scheduler_overlay)
    idf_component_get_property(brookesia_lib espressif__brookesia_lib_utils COMPONENT_LIB)
    idf_component_get_property(brookesia_dir espressif__brookesia_lib_utils COMPONENT_DIR)

    set(upstream_scheduler "${brookesia_dir}/src/task_scheduler.cpp")
    set(exception_include "#include <exception>")
    set(platform_includes [=[#include <exception>
#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif]=])
    set(interruptible_sleep
        "boost::this_thread::sleep_for(boost::chrono::milliseconds(poll_interval_ms));")
    set(allocation_free_sleep [=[#if defined(ESP_PLATFORM)
                        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
#else
                        boost::this_thread::sleep_for(
                            boost::chrono::milliseconds(poll_interval_ms));
#endif]=])

    file(READ "${upstream_scheduler}" scheduler_source)
    string(FIND "${scheduler_source}" "${exception_include}" include_position)
    if(include_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia task scheduler includes changed; review the idle overlay")
    endif()
    string(FIND "${scheduler_source}" "${interruptible_sleep}" sleep_position)
    if(sleep_position EQUAL -1)
        message(FATAL_ERROR
            "Brookesia task scheduler changed; review the allocation-free idle overlay")
    endif()
    string(REPLACE "${exception_include}" "${platform_includes}"
        scheduler_source "${scheduler_source}")
    string(REPLACE "${interruptible_sleep}" "${allocation_free_sleep}"
        scheduler_source "${scheduler_source}")

    set(generated_dir "${CMAKE_BINARY_DIR}/generated_overrides/brookesia_lib_utils")
    set(generated_scheduler "${generated_dir}/task_scheduler.cpp")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_scheduler}" "${scheduler_source}")

    get_target_property(scheduler_sources ${brookesia_lib} SOURCES)
    list(REMOVE_ITEM scheduler_sources "${upstream_scheduler}")
    set_property(TARGET ${brookesia_lib} PROPERTY SOURCES "${scheduler_sources}")
    target_sources(${brookesia_lib} PRIVATE "${generated_scheduler}")
endfunction()
