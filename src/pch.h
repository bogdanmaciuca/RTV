#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <shaderc/shaderc.h>

#ifdef _NDEBUG
#define RTV_DEBUG 0
#else
#define RTV_DEBUG 1
#endif

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef float     f32;
typedef double    f64;
typedef uint8_t   byte;

#define ZERO_MEM(ptr) memset(ptr, 0, sizeof(*ptr))

