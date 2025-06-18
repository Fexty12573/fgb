cmake_minimum_required(VERSION 3.21)

project(gbit C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(libgbit STATIC lib/tester.c lib/inputstate.c lib/ref_cpu.c lib/disassembler.c)

target_include_directories(libgbit PUBLIC lib)
