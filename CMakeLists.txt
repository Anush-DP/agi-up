cmake_minimum_required(VERSION 3.16)
project(agi_up C)

set(CMAKE_C_STANDARD 23)

# Try to find an installed GLFW first (CONFIG or module)
find_package(glfw3 QUIET CONFIG)

if(NOT glfw3_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8 # pin a stable tag; change if you want latest
  )
  FetchContent_MakeAvailable(glfw)
  # GLFW provides target 'glfw' (or 'glfw::glfw' depending on build). Expose both.
  if(TARGET glfw)
    set(GLFW_TARGET glfw)
  elseif(TARGET glfw::glfw)
    set(GLFW_TARGET glfw::glfw)
  else()
    message(FATAL_ERROR "Fetched GLFW but no target glfw or glfw::glfw available")
  endif()
else()
  # If found via config/module, prefer target name 'glfw' if available else assume 'glfw'
  if(TARGET glfw)
    set(GLFW_TARGET glfw)
  else()
    set(GLFW_TARGET glfw)
  endif()
endif()

add_executable(agi_up
        main.c
        agi.c
        agi.h
        render.c
        render.h
        hybrid.c
        hybrid.h)

target_link_libraries(agi_up
        PRIVATE
        ${GLFW_TARGET}
)

if(APPLE)
  target_link_libraries(agi_up PRIVATE "-framework OpenGL" "-framework Cocoa" "-framework IOKit")
endif()
