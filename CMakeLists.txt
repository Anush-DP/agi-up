cmake_minimum_required(VERSION 4.0)
project(agi_up C)

set(CMAKE_C_STANDARD 23)

find_package(glfw3 REQUIRED)

add_executable(agi_up
        main.c
        agi.c
        agi.h
        render.c
        render.h
        hybrid.c
        hybrid.h)

target_link_libraries(agi_up
        glfw
        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
)