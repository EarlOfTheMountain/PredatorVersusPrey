# pixelduel -- build with SDL3
#
# FreeBSD:  pkg install sdl3         (CMake finds it automatically under /usr/local)
# Build:    cmake -B build && cmake --build build
# Run:      ./build/pixelduel        (or: ./build/pixelduel --selftest)
#
# If SDL3 lives somewhere unusual, point CMake at its cmake config:
#   cmake -B build -DCMAKE_PREFIX_PATH=/prefix/containing/lib/cmake/SDL3

cmake_minimum_required(VERSION 3.16)
project(pixelduel VERSION 0.2.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
# gnu17: matches FreeBSD's clang default and keeps POSIX bits (setenv) visible
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


find_package(SDL3 REQUIRED CONFIG)

add_executable(pixelduel
        src/main.c
        src/game.c
        src/player.c
        src/input.c)

target_include_directories(pixelduel PRIVATE include)
target_link_libraries(pixelduel PRIVATE SDL3::SDL3)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pixelduel PRIVATE -Wall -Wextra)
endif()
