cmake_minimum_required(VERSION 3.21)

project(fgb C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(GLFW_LIBRARY_TYPE "STATIC")
set(ONLY_LIBS ON)
set(glew-cmake_BUILD_SHARED OFF)
set(IMGUI_STATIC ON)

add_subdirectory(external/cimgui)
add_subdirectory(external/glfw)
add_subdirectory(external/glew/)
add_subdirectory(external/gbit)
add_subdirectory(lib)
add_subdirectory(test)
add_subdirectory(app)

set(IMGUI_BACKEND_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/cimgui/imgui/backends")

set(IMGUI_BACKEND_SOURCES 
    ${IMGUI_BACKEND_DIR}/imgui_impl_glfw.cpp
    ${IMGUI_BACKEND_DIR}/imgui_impl_opengl3.cpp
)

add_executable(${PROJECT_NAME} src/main.c src/render.c src/audio.c ${IMGUI_BACKEND_SOURCES})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    CIMGUI_USE_GLFW
    CIMGUI_USE_OPENGL3
    CIMGUI_DEFINE_ENUMS_AND_STRUCTS
)

target_include_directories(${PROJECT_NAME} PRIVATE external)

target_link_libraries(${PROJECT_NAME} PRIVATE
    libfgb
    cimgui
    glfw
    libglew_static
    microlog::microlog
)

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /wd5287)
    target_compile_definitions(${PROJECT_NAME} PRIVATE 
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
endif()

add_dependencies(${PROJECT_NAME} rom)
