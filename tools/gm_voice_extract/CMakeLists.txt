cmake_minimum_required(VERSION 3.15)
project(gm_extract LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(gm_extract src/main.cpp)
target_include_directories(gm_extract PRIVATE src)

# TinySoundFont (vendored single header) needs the math lib on POSIX.
if(NOT MSVC)
    find_library(MATH_LIBRARY m)
    if(MATH_LIBRARY)
        target_link_libraries(gm_extract PRIVATE ${MATH_LIBRARY})
    endif()
endif()

# Windows: TSF reads files via stdio; ensure C runtime is fine with large files.
if(MSVC)
    target_compile_definitions(gm_extract PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(gm_extract PRIVATE /O2)
else()
    target_compile_options(gm_extract PRIVATE -O2)
endif()

install(TARGETS gm_extract RUNTIME DESTINATION bin)
