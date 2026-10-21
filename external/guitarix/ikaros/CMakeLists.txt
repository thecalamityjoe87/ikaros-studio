# Guitarix's amp DSP (GPL 2 or later): the amp models, tone stacks and
# cabinet IRs of Ikaros Studio's Amp effect, built as a static library. The
# sources in ../src are unchanged (see ../README.ikaros); this folder is ours.
cmake_minimum_required(VERSION 3.22)

add_library(ikaros_guitarix STATIC ${CMAKE_CURRENT_LIST_DIR}/GuitarixAmp.cpp)

target_compile_features(ikaros_guitarix PRIVATE cxx_std_20)
target_include_directories(ikaros_guitarix
    PUBLIC  ${CMAKE_CURRENT_LIST_DIR}                              # GuitarixAmp.h
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../src/LV2/DSP               # valve.h, cab_data.cc
            ${CMAKE_CURRENT_LIST_DIR}/../src/LV2/DSP/tube_tables   # what valve.h includes
            ${CMAKE_CURRENT_LIST_DIR}/../src/LV2/faust-generated   # the models and tone stacks
            ${CMAKE_CURRENT_LIST_DIR}/../../gxplugins/ikaros)      # GxFaustSupport.h, shared with the pedals
set_target_properties(ikaros_guitarix PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Generated third-party code: don't drown our build output in its warnings.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ikaros_guitarix PRIVATE -w)
elseif(MSVC)
    # /bigobj: one file holds every model and the tube tables.
    target_compile_options(ikaros_guitarix PRIVATE /w /bigobj)
endif()
