# Predator Versus Prey (pvp) -- build with SDL3
#
# FreeBSD:  pkg install sdl3 cmake
# Debian:   apt install libsdl3-dev cmake
# Arch:     pacman -S sdl3 cmake
# Build:    cmake -B build && cmake --build build
# Run:      ./build/pvp                       (built-in ring arena)
#           ./build/pvp maps/duel.tmj         (a Tiled map)
#           ./build/pvp --selftest            (headless smoke test)
#
# If SDL3 lives somewhere unusual, point CMake at its cmake config:
#   cmake -B build -DCMAKE_PREFIX_PATH=/prefix/containing/lib/cmake/SDL3

cmake_minimum_required(VERSION 3.16)
project(PredatorVersusPrey VERSION 0.3.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
# gnu17: matches FreeBSD's clang default and keeps POSIX bits (setenv) visible
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(SDL3 REQUIRED CONFIG)

add_executable(pvp
        src/main.c
        src/game.c
        src/player.c
        src/input.c
        src/arena.c)

target_include_directories(pvp PRIVATE include)
target_link_libraries(pvp PRIVATE SDL3::SDL3)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pvp PRIVATE -Wall -Wextra)
endif()
