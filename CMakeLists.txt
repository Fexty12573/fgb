cmake_minimum_required(VERSION 3.21)

project(fgb C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_subdirectory(external/gbit)
add_subdirectory(lib)
add_subdirectory(test)

add_executable(${PROJECT_NAME} src/main.c "include/fgb/cart.h" "include/fgb/emu.h")

target_link_libraries(${PROJECT_NAME} PRIVATE libfgb)
