cmake_minimum_required(VERSION 3.10)
project(KolibriAI VERSION 0.1.0 LANGUAGES C)

# Стандарт C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Опции сборки
option(BUILD_CLI "Build command-line interface" ON)
option(BUILD_TESTS "Build test suite" OFF)
option(ENABLE_OPTIMIZATIONS "Enable compiler optimizations" ON)
option(USE_MULTITHREADING "Enable multithreading support" OFF)

# Флаги компилятора
if(ENABLE_OPTIMIZATIONS)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-O3 -march=native)
    elseif(MSVC)
        add_compile_options(/O2)
    endif()
endif()

# Потоки
if(USE_MULTITHREADING)
    find_package(Threads REQUIRED)
endif()

# Исходные файлы ядра
set(KOLIBRI_CORE_SOURCES
    src/kolibri_core.c
)

# Заголовочные файлы
set(KOLIBRI_HEADERS
    include/kolibri_core.h
)

# Создание библиотеки
add_library(kolibri_core STATIC ${KOLIBRI_CORE_SOURCES})
target_include_directories(kolibri_core PUBLIC ${CMAKE_SOURCE_DIR}/include)

if(USE_MULTITHREADING)
    target_link_libraries(kolibri_core PRIVATE Threads::Threads)
endif()

# CLI приложение
if(BUILD_CLI)
    add_executable(kolibri src/main.c)
    target_link_libraries(kolibri PRIVATE kolibri_core)
    
    # Установка бинарника
    install(TARGETS kolibri DESTINATION bin)
endif()

# Тесты
if(BUILD_TESTS)
    enable_testing()
    add_executable(kolibri_tests tests/test_kolibri.c)
    target_link_libraries(kolibri_tests PRIVATE kolibri_core)
    add_test(NAME KolibriTests COMMAND kolibri_tests)
endif()

# Информация о проекте
message(STATUS "Kolibri AI v${PROJECT_VERSION}")
message(STATUS "Build CLI: ${BUILD_CLI}")
message(STATUS "Build Tests: ${BUILD_TESTS}")
message(STATUS "Optimizations: ${ENABLE_OPTIMIZATIONS}")
message(STATUS "Multithreading: ${USE_MULTITHREADING}")

# Упаковка
set(CPACK_PACKAGE_NAME "KolibriAI")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance compression tool with AGI capabilities")
include(CPack)
