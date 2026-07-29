set(_STOPWATCH_VOICE_KEY_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(stopwatch_configure_voice_key target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "Unknown firmware target: ${target}")
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(DEFINED ENV{STOPWATCH_VOICE_KEY_FILE} AND
       NOT "$ENV{STOPWATCH_VOICE_KEY_FILE}" STREQUAL "")
        set(_default_key_file "$ENV{STOPWATCH_VOICE_KEY_FILE}")
    else()
        set(_default_key_file
            "$ENV{HOME}/Library/Application Support/StopWatch Codex Agent/device-key.hex"
        )
    endif()
    set(STOPWATCH_VOICE_KEY_FILE "${_default_key_file}" CACHE FILEPATH
        "External StopWatch voice key file"
    )

    set(_generated_include "${CMAKE_CURRENT_BINARY_DIR}/generated/stopwatch_voice")
    set(_generated_header "${_generated_include}/voice_input_device_key.h")
    set(_provisioner
        "${_STOPWATCH_VOICE_KEY_MODULE_DIR}/../../scripts/provision_voice_key.py"
    )
    file(MAKE_DIRECTORY "${_generated_include}")

    set(_key_target "${target}_voice_key_header")
    add_custom_target("${_key_target}" ALL
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${_generated_header}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "STOPWATCH_VOICE_KEY_FILE=${STOPWATCH_VOICE_KEY_FILE}"
            "${Python3_EXECUTABLE}" "${_provisioner}"
            firmware-header --output "${_generated_header}"
        BYPRODUCTS "${_generated_header}"
        COMMENT "Validating the external StopWatch voice key"
        VERBATIM
    )
    add_dependencies("${target}" "${_key_target}")
    target_include_directories("${target}" PRIVATE "${_generated_include}")
endfunction()
