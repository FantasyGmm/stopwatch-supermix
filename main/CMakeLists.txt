file(GLOB_RECURSE MY_SRCS
    "apps/*.c"
    "apps/*.cc"
    "apps/*.cpp"
    "assets/*.c"
    "assets/*.cc"
    "assets/*.cpp"
    "hal/*.c"
    "hal/*.cc"
    "hal/*.cpp"
    "runtime/*.c"
    "runtime/*.cc"
    "runtime/*.cpp"
)

if(CONFIG_STOPWATCH_DEBUG_COMMANDS)
    file(GLOB_RECURSE DEBUG_SRCS
        "debug/*.c"
        "debug/*.cc"
        "debug/*.cpp"
    )
    list(APPEND MY_SRCS ${DEBUG_SRCS})
endif()

set(MY_INCS
    "."
)

if(NOT Python3_EXECUTABLE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
endif()
set(STOPWATCH_VOICE_GENERATED_INCLUDE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/stopwatch_voice"
)
set(STOPWATCH_VOICE_KEY_HEADER
    "${STOPWATCH_VOICE_GENERATED_INCLUDE}/voice_input_device_key.h"
)
file(MAKE_DIRECTORY "${STOPWATCH_VOICE_GENERATED_INCLUDE}")
execute_process(
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/provision_voice_key.py"
        firmware-header
        --output "${STOPWATCH_VOICE_KEY_HEADER}"
    RESULT_VARIABLE STOPWATCH_VOICE_KEY_RESULT
    OUTPUT_QUIET
    ERROR_VARIABLE STOPWATCH_VOICE_KEY_ERROR
)
if(NOT STOPWATCH_VOICE_KEY_RESULT EQUAL 0)
    string(STRIP "${STOPWATCH_VOICE_KEY_ERROR}" STOPWATCH_VOICE_KEY_ERROR)
    message(FATAL_ERROR
        "StopWatch production voice key validation failed: "
        "${STOPWATCH_VOICE_KEY_ERROR}"
    )
endif()
list(APPEND MY_INCS "${STOPWATCH_VOICE_GENERATED_INCLUDE}")

set(CODEX_CJK_FONT
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/font_puhui_14_1.c"
)

idf_component_register(
    SRCS 
        "main.cpp"
        ${MY_SRCS}
        ${CODEX_CJK_FONT}
    INCLUDE_DIRS 
        ${MY_INCS}
    EMBED_FILES
        "assets/sfx/boot_sfx.bin"
    EMBED_TXTFILES
        "hal/utils/config_ap/assets/badge_config_ap.html"
)

include(cmake/unified_assets.cmake)
