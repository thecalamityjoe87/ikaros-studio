/* Ikaros Studio: a portable config.h for building LAME's encoder library
   with CMake (LAME's own build makes this file with autotools). Only what
   libmp3lame needs; no decoder (mpglib) and no hand-written SIMD. */
#ifndef IKAROS_LAME_CONFIG_H
#define IKAROS_LAME_CONFIG_H

#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1
#define LAME_LIBRARY_BUILD 1
#define PACKAGE "lame"
#define VERSION "3.100"

/* LAME's name for a 32-bit IEEE float. */
#define ieee754_float32_t float

#endif
