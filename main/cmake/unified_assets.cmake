set(UNIFIED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
set(UNIFIED_ASSETS_SCRIPT
    "${PROJECT_DIR}/tools/xiaozhi/build_default_assets.py"
)
set(UNIFIED_SR_MODEL_PATH
    "${PROJECT_DIR}/managed_components/espressif__esp-sr/model"
)
set(UNIFIED_ASSETS_MAX_SIZE 4194304)

# The existing generator emits a zero-byte file when there are no assets. Keep
# the assets partition artifact valid for the unified flash contract even when
# the standalone Assistant and its wake-word model are disabled.
set(UNIFIED_ASSETS_EXTRA_ARGS)
set(UNIFIED_ASSETS_EXTRA_FILES)
if(NOT CONFIG_USE_ESP_WAKE_WORD AND NOT CONFIG_USE_AFE_WAKE_WORD AND
   NOT CONFIG_USE_CUSTOM_WAKE_WORD)
    set(UNIFIED_EMPTY_ASSETS_DIR "${CMAKE_BINARY_DIR}/unified_empty_assets")
    set(UNIFIED_EMPTY_ASSETS_MANIFEST "${UNIFIED_EMPTY_ASSETS_DIR}/empty_assets.json")
    file(MAKE_DIRECTORY "${UNIFIED_EMPTY_ASSETS_DIR}")
    file(WRITE "${UNIFIED_EMPTY_ASSETS_MANIFEST}" "{}\n")
    list(APPEND UNIFIED_ASSETS_EXTRA_ARGS --extra_files "${UNIFIED_EMPTY_ASSETS_DIR}")
    list(APPEND UNIFIED_ASSETS_EXTRA_FILES "${UNIFIED_EMPTY_ASSETS_MANIFEST}")
endif()

set(UNIFIED_SR_MODEL_FILES)
if(CONFIG_USE_ESP_WAKE_WORD OR CONFIG_USE_AFE_WAKE_WORD OR CONFIG_USE_CUSTOM_WAKE_WORD)
    file(GLOB_RECURSE UNIFIED_SR_MODEL_FILES CONFIGURE_DEPENDS
        "${UNIFIED_SR_MODEL_PATH}/*"
    )
endif()

add_custom_command(
    OUTPUT "${UNIFIED_ASSETS_BIN}"
    COMMAND "${PYTHON}" "${UNIFIED_ASSETS_SCRIPT}"
            "--sdkconfig" "${SDKCONFIG}"
            "--output" "${UNIFIED_ASSETS_BIN}"
            "--esp_sr_model_path" "${UNIFIED_SR_MODEL_PATH}"
            ${UNIFIED_ASSETS_EXTRA_ARGS}
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${UNIFIED_ASSETS_BIN}"
            "-DMAX_SIZE=${UNIFIED_ASSETS_MAX_SIZE}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/check_binary_size.cmake"
    DEPENDS
        "${SDKCONFIG}"
        "${UNIFIED_ASSETS_SCRIPT}"
        "${CMAKE_CURRENT_LIST_DIR}/check_binary_size.cmake"
        ${UNIFIED_ASSETS_EXTRA_FILES}
        ${UNIFIED_SR_MODEL_FILES}
    COMMENT "Building the unified assets partition image"
    VERBATIM
)

add_custom_target(generated_unified_assets ALL DEPENDS "${UNIFIED_ASSETS_BIN}")
