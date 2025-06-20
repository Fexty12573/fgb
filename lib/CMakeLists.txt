cmake_minimum_required(VERSION 3.21)

project(libfgb C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(microlog_DIR ${CMAKE_SOURCE_DIR}/external/microlog)
find_package(microlog REQUIRED)

set(SOURCES cpu.c instruction.c mmu.c cart.c emu.c)
add_library(libfgb STATIC ${SOURCES})

target_link_libraries(libfgb PUBLIC microlog::microlog)
target_include_directories(libfgb PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(libfgb PRIVATE ${CMAKE_SOURCE_DIR}/include/fgb)
