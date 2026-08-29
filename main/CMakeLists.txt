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

set(CODEX_CJK_FONT
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/AgentPanelTitle16.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/CodexDetail17.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/CodexTaskTitle21.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/font_puhui_14_1.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/NotoSansSCBold16Trae.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PinnedMicTitle24.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/PinnedMicState22.c"
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
