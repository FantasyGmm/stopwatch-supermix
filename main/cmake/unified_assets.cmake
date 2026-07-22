set(UNIFIED_ASSETS_BIN "${CMAKE_BINARY_DIR}/generated_assets.bin")
set(UNIFIED_ASSETS_SCRIPT
    "${PROJECT_DIR}/tools/xiaozhi/build_default_assets.py"
)
set(UNIFIED_SR_MODEL_PATH
    "${PROJECT_DIR}/managed_components/espressif__esp-sr/model"
)
set(UNIFIED_ASSETS_MAX_SIZE 4194304)

file(GLOB_RECURSE UNIFIED_SR_MODEL_FILES CONFIGURE_DEPENDS
    "${UNIFIED_SR_MODEL_PATH}/*"
)

add_custom_command(
    OUTPUT "${UNIFIED_ASSETS_BIN}"
    COMMAND "${PYTHON}" "${UNIFIED_ASSETS_SCRIPT}"
            "--sdkconfig" "${SDKCONFIG}"
            "--output" "${UNIFIED_ASSETS_BIN}"
            "--esp_sr_model_path" "${UNIFIED_SR_MODEL_PATH}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT_FILE=${UNIFIED_ASSETS_BIN}"
            "-DMAX_SIZE=${UNIFIED_ASSETS_MAX_SIZE}"
            "-P" "${CMAKE_CURRENT_LIST_DIR}/check_binary_size.cmake"
    DEPENDS
        "${SDKCONFIG}"
        "${UNIFIED_ASSETS_SCRIPT}"
        "${CMAKE_CURRENT_LIST_DIR}/check_binary_size.cmake"
        ${UNIFIED_SR_MODEL_FILES}
    COMMENT "Building the unified Xiaozhi wake-word assets"
    VERBATIM
)

add_custom_target(generated_unified_assets ALL DEPENDS "${UNIFIED_ASSETS_BIN}")
