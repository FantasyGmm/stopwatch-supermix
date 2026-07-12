# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(StopWatch-UserDemo)

idf_component_get_property(bt_lib bt COMPONENT_LIB)
if(bt_lib)
    target_compile_options(${bt_lib} PRIVATE -Wno-error=array-bounds)
endif()
