function(apply_audio_processor_light_overlay)
    idf_component_get_property(audio_processor_lib
        jason-mao__av_processor COMPONENT_LIB)
    idf_component_get_property(audio_processor_dir
        jason-mao__av_processor COMPONENT_DIR)

    set(upstream_processor "${audio_processor_dir}/src/audio_processor_light.c")
    set(task_include [=[#include "freertos/task.h"]=])
    set(psram_task_includes [=[#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "esp_heap_caps.h"]=])
    set(internal_worker_create [=[    BaseType_t ok = xTaskCreatePinnedToCore(audio_worker_task, "audio_light_worker",
                                            stack, NULL, prio, &audio_manager.worker_task, core);]=])
    set(psram_worker_create [=[    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        audio_worker_task, "audio_light_worker", stack, NULL, prio,
        &audio_manager.worker_task, core, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);]=])
    set(standard_self_delete "    vTaskDelete(NULL);")
    set(caps_self_delete "    vTaskDeleteWithCaps(NULL);")

    file(READ "${upstream_processor}" processor_source)
    foreach(required_text IN ITEMS
            "${task_include}" "${internal_worker_create}" "${standard_self_delete}")
        string(FIND "${processor_source}" "${required_text}" required_position)
        if(required_position EQUAL -1)
            message(FATAL_ERROR
                "Light audio processor changed; review the PSRAM worker overlay")
        endif()
    endforeach()

    string(REPLACE "${task_include}" "${psram_task_includes}"
        processor_source "${processor_source}")
    string(REPLACE "${internal_worker_create}" "${psram_worker_create}"
        processor_source "${processor_source}")
    string(REPLACE "${standard_self_delete}" "${caps_self_delete}"
        processor_source "${processor_source}")

    set(generated_dir
        "${CMAKE_BINARY_DIR}/generated_overrides/audio_processor_light")
    set(generated_processor "${generated_dir}/audio_processor_light.c")
    file(MAKE_DIRECTORY "${generated_dir}")
    file(WRITE "${generated_processor}" "${processor_source}")

    get_target_property(audio_processor_sources ${audio_processor_lib} SOURCES)
    list(REMOVE_ITEM audio_processor_sources "${upstream_processor}")
    set_property(TARGET ${audio_processor_lib} PROPERTY SOURCES
        "${audio_processor_sources}")
    target_sources(${audio_processor_lib} PRIVATE "${generated_processor}")
endfunction()
