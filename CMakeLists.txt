cmake_minimum_required(VERSION 3.21)

project(fgb C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES
    "src/main.c"
    "src/fgb/cpu.c")

add_executable(${PROJECT_NAME} ${SOURCES})
