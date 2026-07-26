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
    set(upstream_feeder_queue [=[#define AUDIO_LIGHT_FEED_QUEUE_LEN       (2)]=])
    # The UDP receive task feeds this queue synchronously. Two 60 ms Opus packets
    # are not enough to absorb decoder, recorder, and scheduler jitter.
    set(buffered_feeder_queue [=[#define AUDIO_LIGHT_FEED_QUEUE_LEN       (8)]=])
    set(upstream_worker_defaults [=[#define AUDIO_LIGHT_WORKER_STACK_DEFAULT (2048 * 12)
#define AUDIO_LIGHT_WORKER_PRIO_DEFAULT  (5)]=])
    # Opus decode overflows an 8 KiB stack on ESP32-S3, so retain the
    # upstream audio worker's 24 KiB minimum for the dedicated feeder.
    set(split_worker_defaults [=[#define AUDIO_LIGHT_WORKER_STACK_DEFAULT        (2048 * 12)
#define AUDIO_LIGHT_WORKER_PRIO_DEFAULT         (5)
#define AUDIO_LIGHT_FEEDER_WORKER_STACK_DEFAULT (2048 * 12)
#define AUDIO_LIGHT_FEEDER_WORKER_PRIO_DEFAULT  (6)]=])
    set(upstream_feeder_struct_tail [=[    uint8_t                 *dec_out_buf;
    size_t                   dec_out_buf_size;
};]=])
    set(split_feeder_struct_tail [=[    uint8_t                 *dec_out_buf;
    size_t                   dec_out_buf_size;
    TaskHandle_t             worker_task;
    SemaphoreHandle_t        worker_stopped;
    bool                     worker_stop;
};]=])
    set(audio_worker_anchor [=[static void audio_worker_task(void *arg)]=])
    set(split_feeder_worker [=[static void audio_feeder_worker_notify(audio_feeder_handle_t fd)
{
    if (fd != NULL && fd->worker_task != NULL) {
        xTaskNotifyGive(fd->worker_task);
    }
}

static void audio_feeder_worker_task(void *arg)
{
    audio_feeder_handle_t fd = (audio_feeder_handle_t)arg;
    while (!fd->worker_stop) {
        if (fd->state != AUDIO_RUN_STATE_PLAYING) {
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(AUDIO_LIGHT_POLL_MS));
            continue;
        }

        audio_light_packet_t *pkt = NULL;
        if (xQueueReceive(fd->in_queue, &pkt, pdMS_TO_TICKS(AUDIO_LIGHT_POLL_MS)) != pdTRUE) {
            continue;
        }

        if (audio_manager.lock != NULL) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            audio_manager.feeder_busy = true;
            xSemaphoreGive(audio_manager.lock);
        }
        if (audio_feeder_process_packet(fd, pkt) != ESP_OK) {
            ESP_LOGE(TAG, "Feeder packet processing failed");
        }
        audio_light_packet_free(pkt);
        if (audio_manager.lock != NULL) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            audio_manager.feeder_busy = false;
            xSemaphoreGive(audio_manager.lock);
        }
    }

    if (audio_manager.lock != NULL) {
        xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
        audio_manager.feeder_busy = false;
        xSemaphoreGive(audio_manager.lock);
    }
    xSemaphoreGive(fd->worker_stopped);
    vTaskSuspend(NULL);
}

static esp_err_t audio_feeder_worker_ensure_started(
    audio_feeder_handle_t fd, const audio_task_config_t *task_cfg)
{
    int stack = AUDIO_LIGHT_FEEDER_WORKER_STACK_DEFAULT;
    int prio = AUDIO_LIGHT_FEEDER_WORKER_PRIO_DEFAULT;
    BaseType_t core = tskNO_AFFINITY;
    if (task_cfg != NULL) {
        if (task_cfg->task_stack > stack) {
            stack = task_cfg->task_stack;
        }
        if (task_cfg->task_prio > prio) {
            prio = task_cfg->task_prio;
        }
        if (task_cfg->task_core >= 0) {
            core = task_cfg->task_core;
        }
    }

    fd->worker_stopped = xSemaphoreCreateBinary();
    if (fd->worker_stopped == NULL) {
        ESP_LOGE(TAG, "Failed to create light feeder stop signal");
        return ESP_ERR_NO_MEM;
    }
    fd->worker_stop = false;
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(
        audio_feeder_worker_task, "audio_light_feeder", stack, fd, prio,
        &fd->worker_task, core, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) {
        fd->worker_task = NULL;
        vSemaphoreDelete(fd->worker_stopped);
        fd->worker_stopped = NULL;
        ESP_LOGE(TAG, "Failed to create light feeder task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void audio_worker_task(void *arg)]=])
    set(upstream_worker_snapshot [=[        audio_recorder_handle_t rec = NULL;
        audio_feeder_handle_t fd = NULL;
        bool stop = false;
        if (audio_manager.lock) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            rec = audio_manager.recorder;
            fd = audio_manager.feeder;
            audio_manager.recorder_busy = rec != NULL;
            audio_manager.feeder_busy = fd != NULL;
            stop = audio_manager.worker_stop;
            xSemaphoreGive(audio_manager.lock);
        }]=])
    set(recorder_worker_snapshot [=[        audio_recorder_handle_t rec = NULL;
        bool stop = false;
        if (audio_manager.lock) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            rec = audio_manager.recorder;
            audio_manager.recorder_busy = rec != NULL;
            stop = audio_manager.worker_stop;
            xSemaphoreGive(audio_manager.lock);
        }]=])
    set(upstream_feeder_work [=[        if (fd && fd->state == AUDIO_RUN_STATE_PLAYING) {
            audio_light_packet_t *pkt = NULL;
            if (xQueueReceive(fd->in_queue, &pkt, 0) == pdTRUE) {
                if (audio_feeder_process_packet(fd, pkt) != ESP_OK) {
                    ESP_LOGE(TAG, "Feeder packet processing failed");
                }
                audio_light_packet_free(pkt);
                did_work = true;
            }
        }

]=])
    set(upstream_worker_loop_cleanup [=[        if (audio_manager.lock) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            audio_manager.recorder_busy = false;
            audio_manager.feeder_busy = false;
            xSemaphoreGive(audio_manager.lock);
        }]=])
    set(recorder_worker_loop_cleanup [=[        if (audio_manager.lock) {
            xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
            audio_manager.recorder_busy = false;
            xSemaphoreGive(audio_manager.lock);
        }]=])
    set(upstream_worker_exit_cleanup [=[        audio_manager.worker_task = NULL;
        audio_manager.worker_stop = false;
        audio_manager.recorder_busy = false;
        audio_manager.feeder_busy = false;]=])
    set(recorder_worker_exit_cleanup [=[        audio_manager.worker_task = NULL;
        audio_manager.worker_stop = false;
        audio_manager.recorder_busy = false;]=])
    set(upstream_feeder_start [=[    ESP_GOTO_ON_ERROR(audio_worker_ensure_started(&config->feeder_task_config), err, TAG, "Failed to start worker");

    xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
    audio_manager.feeder = fd;
    xSemaphoreGive(audio_manager.lock);
    fd->state = AUDIO_RUN_STATE_IDLE;]=])
    set(split_feeder_start [=[    fd->state = AUDIO_RUN_STATE_IDLE;
    ESP_GOTO_ON_ERROR(audio_feeder_worker_ensure_started(fd, &config->feeder_task_config),
                      err, TAG, "Failed to start feeder worker");

    xSemaphoreTake(audio_manager.lock, portMAX_DELAY);
    audio_manager.feeder = fd;
    xSemaphoreGive(audio_manager.lock);]=])
    set(upstream_feeder_close_state [=[    fd->state = AUDIO_RUN_STATE_CLOSED;
    while (audio_manager.lock) {]=])
    set(split_feeder_close_state [=[    fd->state = AUDIO_RUN_STATE_CLOSED;
    fd->worker_stop = true;
    audio_feeder_worker_notify(fd);
    if (fd->worker_task != NULL) {
        xSemaphoreTake(fd->worker_stopped, portMAX_DELAY);
        vTaskDeleteWithCaps(fd->worker_task);
        fd->worker_task = NULL;
    }
    if (fd->worker_stopped != NULL) {
        vSemaphoreDelete(fd->worker_stopped);
        fd->worker_stopped = NULL;
    }
    while (audio_manager.lock) {]=])
    set(upstream_feeder_start_notify [=[esp_err_t audio_feeder_start(audio_feeder_handle_t fd)
{
    ESP_RETURN_ON_FALSE(fd != NULL && fd->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Feeder not opened");
    fd->state = AUDIO_RUN_STATE_PLAYING;
    audio_worker_notify();
    return ESP_OK;
}]=])
    set(split_feeder_start_notify [=[esp_err_t audio_feeder_start(audio_feeder_handle_t fd)
{
    ESP_RETURN_ON_FALSE(fd != NULL && fd->state != AUDIO_RUN_STATE_CLOSED, ESP_FAIL, TAG, "Feeder not opened");
    fd->state = AUDIO_RUN_STATE_PLAYING;
    audio_feeder_worker_notify(fd);
    return ESP_OK;
}]=])
    set(upstream_feed_tail [=[    audio_worker_notify();
    return ESP_OK;
}

esp_err_t audio_processor_mixer_open]=])
    set(split_feed_tail [=[    audio_feeder_worker_notify(fd);
    return ESP_OK;
}

esp_err_t audio_processor_mixer_open]=])
    set(standard_self_delete "    vTaskDelete(NULL);")
    set(caps_self_delete "    vTaskDeleteWithCaps(NULL);")

    file(READ "${upstream_processor}" processor_source)
    foreach(required_text IN ITEMS
            "${task_include}" "${internal_worker_create}" "${upstream_feeder_queue}"
            "${upstream_worker_defaults}" "${upstream_feeder_struct_tail}"
            "${audio_worker_anchor}" "${upstream_worker_snapshot}"
            "${upstream_feeder_work}" "${upstream_worker_loop_cleanup}"
            "${upstream_worker_exit_cleanup}" "${upstream_feeder_start}"
            "${upstream_feeder_close_state}" "${upstream_feeder_start_notify}"
            "${upstream_feed_tail}" "${standard_self_delete}")
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
    string(REPLACE "${upstream_feeder_queue}" "${buffered_feeder_queue}"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_worker_defaults}" "${split_worker_defaults}"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_feeder_struct_tail}" "${split_feeder_struct_tail}"
        processor_source "${processor_source}")
    string(REPLACE "${audio_worker_anchor}" "${split_feeder_worker}"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_worker_snapshot}" "${recorder_worker_snapshot}"
        processor_source "${processor_source}")
    string(REPLACE "if (stop && rec == NULL && fd == NULL)"
        "if (stop && rec == NULL)" processor_source "${processor_source}")
    string(REPLACE "${upstream_feeder_work}" ""
        processor_source "${processor_source}")
    string(REPLACE "${upstream_worker_loop_cleanup}" "${recorder_worker_loop_cleanup}"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_worker_exit_cleanup}" "${recorder_worker_exit_cleanup}"
        processor_source "${processor_source}")
    string(REPLACE
        "audio_manager.recorder == NULL && audio_manager.feeder == NULL && audio_manager.worker_task"
        "audio_manager.recorder == NULL && audio_manager.worker_task"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_feeder_start}" "${split_feeder_start}"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_feeder_close_state}" "${split_feeder_close_state}"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_feeder_start_notify}" "${split_feeder_start_notify}"
        processor_source "${processor_source}")
    string(REPLACE "${upstream_feed_tail}" "${split_feed_tail}"
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
