cmake_minimum_required(VERSION 3.21)

project(fgb C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(GLFW_LIBRARY_TYPE "STATIC")
set(ONLY_LIBS ON)
set(glew-cmake_BUILD_SHARED OFF)

add_subdirectory(external/glfw)
add_subdirectory(external/glew/)
add_subdirectory(external/gbit)
add_subdirectory(lib)
add_subdirectory(test)

add_executable(${PROJECT_NAME} src/main.c src/render.c "include/render.h")

target_link_libraries(${PROJECT_NAME} PRIVATE libfgb glfw libglew_static microlog::microlog)
