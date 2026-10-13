# LAME's MP3 encoder library (libmp3lame), built as a static library for
# Ikaros Studio's MP3 export. The sources in ../ are LAME 3.100, unchanged
# (see ../README.ikaros); this file and config.h are ours.
cmake_minimum_required(VERSION 3.22)

set(LAME_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

add_library(mp3lame STATIC
    ${LAME_DIR}/libmp3lame/VbrTag.c
    ${LAME_DIR}/libmp3lame/bitstream.c
    ${LAME_DIR}/libmp3lame/encoder.c
    ${LAME_DIR}/libmp3lame/fft.c
    ${LAME_DIR}/libmp3lame/gain_analysis.c
    ${LAME_DIR}/libmp3lame/id3tag.c
    ${LAME_DIR}/libmp3lame/lame.c
    ${LAME_DIR}/libmp3lame/newmdct.c
    ${LAME_DIR}/libmp3lame/presets.c
    ${LAME_DIR}/libmp3lame/psymodel.c
    ${LAME_DIR}/libmp3lame/quantize.c
    ${LAME_DIR}/libmp3lame/quantize_pvt.c
    ${LAME_DIR}/libmp3lame/reservoir.c
    ${LAME_DIR}/libmp3lame/set_get.c
    ${LAME_DIR}/libmp3lame/tables.c
    ${LAME_DIR}/libmp3lame/takehiro.c
    ${LAME_DIR}/libmp3lame/util.c
    ${LAME_DIR}/libmp3lame/vbrquantize.c
    ${LAME_DIR}/libmp3lame/version.c)

target_compile_definitions(mp3lame PRIVATE HAVE_CONFIG_H)
target_include_directories(mp3lame
    PRIVATE ${CMAKE_CURRENT_LIST_DIR} ${LAME_DIR}/libmp3lame
    PUBLIC  ${LAME_DIR}/include)           # lame.h
set_target_properties(mp3lame PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Third-party code: don't drown our build output in its warnings.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mp3lame PRIVATE -w)
elseif(MSVC)
    target_compile_options(mp3lame PRIVATE /w)
endif()

if(UNIX)
    target_link_libraries(mp3lame PRIVATE m)
endif()
