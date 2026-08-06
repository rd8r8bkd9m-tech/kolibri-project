cmake_minimum_required(VERSION 3.16)
project(kolibri_estimate_memory LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(SQLite3 REQUIRED)

add_library(kolibri_estimate_memory STATIC
    src/estimate_memory.c
)
target_include_directories(kolibri_estimate_memory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kolibri_estimate_memory PUBLIC SQLite::SQLite3)

option(KEM_BUILD_TESTS "Build estimate-memory tests" ON)
if(KEM_BUILD_TESTS)
    enable_testing()
    add_executable(test_estimate_memory tests/test_estimate_memory.c)
    target_link_libraries(test_estimate_memory PRIVATE kolibri_estimate_memory SQLite::SQLite3)
    add_test(NAME test_estimate_memory COMMAND test_estimate_memory)
endif()
