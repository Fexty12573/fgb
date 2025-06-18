cmake_minimum_required(VERSION 3.21)

project(fgb C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(microlog_DIR ${CMAKE_SOURCE_DIR}/external/microlog)

find_package(microlog REQUIRED)

set(SOURCES
    "src/main.c"
    "src/fgb/cpu.c"
    "src/fgb/memory.c"
    "src/fgb/instruction.c")

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE microlog::microlog)
