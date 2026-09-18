#pragma once

#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <shaderc/shaderc.h>

#include "../vendor/stb/stb_truetype.h"

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
#define ALIGN(x, align) ((byte *)(((uintptr_t)(x) + ((uintptr_t)(align) - 1)) & ~((uintptr_t)(align) - 1)))
#define ALIGNOF(T) offsetof(struct { char c; T member; }, member)
