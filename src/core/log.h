#pragma once

#define LOG(level, fmt, ...)                         \
    fprintf(stderr, "[%s][%s:%d][%s())]: " fmt "\n", \
            (level), __FILE__, __LINE__, __func__, ##__VA_ARGS__)

#define INFO(fmt, ...) LOG("INFO", fmt, ##__VA_ARGS__)

#define WARN(fmt, ...) LOG("WARN", fmt, ##__VA_ARGS__)

#define ERROR(fmt, ...) LOG("ERROR", fmt, ##__VA_ARGS__)

#define CHECK(x, fmt, ...) \
    ((x) ? 1 : (ERROR("Check failed: [%s] " fmt, #x, ##__VA_ARGS__), 0))

#define SDL_CHECK(x) \
    ((x) ? 1 : (ERROR("[%s] (SDL_Error: %s)", #x, SDL_GetError()), 0))

