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

# Keep the retired Clock sources upstream, but leave them out of the firmware.
list(FILTER MY_SRCS EXCLUDE REGEX "/apps/(app_clock|app_alarm_clock|app_stopwatch|app_watch_face)/")
list(REMOVE_ITEM MY_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/icon_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/alarm_icon.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/classic/classic_hour_hand.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/classic/classic_minute_hand.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/classic/classic_second_hand.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_0.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_1.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_3.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_4.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_5.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_6.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_7.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_8.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/images/watch_face/big_number/big_number_9.c"
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
