# The DSP of the GxPlugins pedals (GPL 3) and two Guitarix effects (GPL 2+), built as a static library for
# Ikaros Studio's pedalboard. The sources in ../<Pedal>/dsp are unchanged
# (see ../README.ikaros); this folder is ours.
cmake_minimum_required(VERSION 3.22)

add_library(ikaros_gxplugins STATIC
    ${CMAKE_CURRENT_LIST_DIR}/GxSD1.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxGuvnor.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxHotBox.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxBottleRocket.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxEternity.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxSunFace.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxSuppaToneBender.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxSuperFuzz.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxQuack.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GxSlowGear.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GuitarixFlanger.cpp
    ${CMAKE_CURRENT_LIST_DIR}/GuitarixReverb.cpp)

target_compile_features(ikaros_gxplugins PRIVATE cxx_std_20)
target_include_directories(ikaros_gxplugins PUBLIC ${CMAKE_CURRENT_LIST_DIR})   # GxDsp.h
set_target_properties(ikaros_gxplugins PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Generated third-party code: don't drown our build output in its warnings
# (unused variables, int/unsigned comparisons).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ikaros_gxplugins PRIVATE -w)
elseif(MSVC)
    target_compile_options(ikaros_gxplugins PRIVATE /w)
endif()
