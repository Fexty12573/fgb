cmake_minimum_required(VERSION 3.21)

project(fgbtest C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(fgbtest test.c "mock_cpu.c")
target_link_libraries(fgbtest libfgb libgbit)
