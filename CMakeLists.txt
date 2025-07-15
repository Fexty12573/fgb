cmake_minimum_required(VERSION 3.21)

project(fgb C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(GLFW_LIBRARY_TYPE "STATIC")

add_subdirectory(external/glfw)
add_subdirectory(external/glew/build/cmake)
add_subdirectory(external/gbit)
add_subdirectory(lib)
add_subdirectory(test)

add_executable(${PROJECT_NAME} src/main.c)

target_link_libraries(${PROJECT_NAME} PRIVATE libfgb glfw glew_s)
