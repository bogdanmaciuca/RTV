cmake_minimum_required(VERSION 3.15)
project(RTV LANGUAGES C)

# Clangd support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL
find_package(SDL3 REQUIRED CONFIG)

# Source files
file(GLOB SRCS
    src/*.c
    src/core/*.c
    src/engine/*.c
    src/engine/generated/*.c
    vendor/**/*.c
)

# Executable target
add_executable(${PROJECT_NAME} ${SRCS})

# PCH
target_precompile_headers(${PROJECT_NAME} PRIVATE src/pch.h)

# Compile options
target_compile_options(${PROJECT_NAME} PRIVATE -Wall)

# Linking
target_link_libraries(${PROJECT_NAME} PRIVATE
    m
    SDL3::SDL3
    shaderc_shared
    stdc++
)

