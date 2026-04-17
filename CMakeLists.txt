cmake_minimum_required(VERSION 3.20)
project(link-export-all VERSION 0.16.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LINK_EXPORT_ALL_BUILD_PROXY "Build the Windows MSVC link proxy executable" ON)

add_library(defgen STATIC
    src/defgen/coff_image.cpp
    src/defgen/coff_parser.cpp
    src/defgen/elf_parser.cpp
    src/defgen/def_generator.cpp
)
target_include_directories(defgen PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/defgen"
)
if(MSVC)
    target_compile_options(defgen PRIVATE /W4 /permissive-)
endif()

if(WIN32 AND LINK_EXPORT_ALL_BUILD_PROXY)
    add_executable(link-export-all src/proxy/main.cpp)
    target_link_libraries(link-export-all PRIVATE defgen)
    target_compile_definitions(link-export-all PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
        target_compile_options(link-export-all PRIVATE /W4 /permissive-)
    endif()
    target_link_libraries(link-export-all PRIVATE Shlwapi.lib)
    set_target_properties(link-export-all PROPERTIES
        OUTPUT_NAME "link-export-all"
        WIN32_EXECUTABLE FALSE
    )
endif()
