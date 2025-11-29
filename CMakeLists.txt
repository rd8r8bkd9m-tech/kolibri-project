cmake_minimum_required(VERSION 3.16)
project(kolibri LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(KOLIBRI_ENABLE_TESTS "Enable Kolibri tests" ON)
option(KOLIBRI_ENABLE_FUZZ "Enable Kolibri fuzz targets" OFF)
option(KOLIBRI_ENABLE_GPU "Enable Kolibri GPU acceleration components" ON)

if(KOLIBRI_ENABLE_GPU)
    set(KOLIBRI_GPU_SOURCES
        engine/gpu_encoder/kolibri_gpu_encoder.c
        engine/gpu_encoder/gpu_encoder_stub.c
    )
    if(APPLE)
        list(APPEND KOLIBRI_GPU_SOURCES engine/gpu_encoder/gpu_encoder_metal.mm)
    endif()

    add_library(kolibri_gpu STATIC ${KOLIBRI_GPU_SOURCES})
    target_include_directories(kolibri_gpu
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/gpu_encoder>
            $<INSTALL_INTERFACE:include>)

    if(APPLE)
        find_library(METAL_FRAMEWORK Metal REQUIRED)
        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        target_link_libraries(kolibri_gpu PRIVATE ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
    endif()
    if(NOT MSVC)
        target_link_libraries(kolibri_gpu PRIVATE m)
    endif()

    add_executable(kolibri_gpu_demo tools/kgpu_demo.c)
    target_link_libraries(kolibri_gpu_demo PRIVATE kolibri_gpu)
endif()

add_library(kolibri_core_objects OBJECT
    backend/src/decimal.c
    backend/src/digits.c
    backend/src/digit_text.c
    backend/src/genome.c
    backend/src/random.c
    backend/src/formula.c
    backend/src/roy.c
    backend/src/script.c
    backend/src/symbol_table.c
    backend/src/net.c
    backend/src/knowledge.c
    backend/src/knowledge_index.c
    backend/src/knowledge_queue.c
    backend/src/sim.c
    backend/src/semantic_digits.c
    backend/src/context_window.c
    backend/src/corpus_learning.c
    backend/src/text_generation.c
    backend/src/compress.c
)

target_include_directories(kolibri_core_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backend/include>
        $<INSTALL_INTERFACE:include>
)
if(DEFINED OPENSSL_INCLUDE_DIR)
    target_include_directories(kolibri_core_objects PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

add_library(kolibri_core STATIC $<TARGET_OBJECTS:kolibri_core_objects>)

target_include_directories(kolibri_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backend/include>
        $<INSTALL_INTERFACE:include>
)

set(OPENSSL_USE_STATIC_LIBS OFF)
if(APPLE AND NOT DEFINED OPENSSL_ROOT_DIR)
    foreach(prefix "/opt/homebrew/opt/openssl@3" "/usr/local/opt/openssl@3")
        if(EXISTS "${prefix}")
            set(OPENSSL_ROOT_DIR "${prefix}" CACHE PATH "OpenSSL root directory")
            break()
        endif()
    endforeach()
endif()
find_package(OpenSSL QUIET COMPONENTS Crypto)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

set(KOLIBRI_OPENSSL_TARGET "")

if(OpenSSL_FOUND)
    if(TARGET OpenSSL::Crypto)
        set(KOLIBRI_OPENSSL_TARGET OpenSSL::Crypto)
    elseif(DEFINED OPENSSL_CRYPTO_LIBRARY)
        add_library(kolibri_openssl INTERFACE)
        target_include_directories(kolibri_openssl INTERFACE ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(kolibri_openssl INTERFACE ${OPENSSL_CRYPTO_LIBRARY})
        set(KOLIBRI_OPENSSL_TARGET kolibri_openssl)
    endif()
endif()

if(NOT KOLIBRI_OPENSSL_TARGET)
    set(_KOLIBRI_OPENSSL_HINTS
        "/opt/homebrew/opt/openssl@3"
        "/usr/local/opt/openssl@3"
        "/opt/homebrew/opt/openssl"
        "/usr/local/opt/openssl"
    )
    foreach(prefix IN LISTS _KOLIBRI_OPENSSL_HINTS)
        if(EXISTS "${prefix}/lib/libcrypto.dylib" OR EXISTS "${prefix}/lib/libcrypto.so")
            add_library(kolibri_openssl INTERFACE)
            target_include_directories(kolibri_openssl INTERFACE "${prefix}/include")
            if(EXISTS "${prefix}/lib/libcrypto.dylib")
                target_link_libraries(kolibri_openssl INTERFACE "${prefix}/lib/libcrypto.dylib")
            else()
                target_link_libraries(kolibri_openssl INTERFACE "${prefix}/lib/libcrypto.so")
            endif()
            set(KOLIBRI_OPENSSL_TARGET kolibri_openssl)
            break()
        endif()
    endforeach()
endif()

if(NOT KOLIBRI_OPENSSL_TARGET)
    message(FATAL_ERROR "OpenSSL Crypto library not found. Set OPENSSL_ROOT_DIR or install openssl@3.")
endif()

target_link_libraries(kolibri_core_objects PUBLIC ${KOLIBRI_OPENSSL_TARGET})
target_link_libraries(kolibri_core PUBLIC ${KOLIBRI_OPENSSL_TARGET} SQLite::SQLite3 m)

add_library(kolibri_wasm STATIC
    backend/src/wasm_bridge.c
    backend/src/wasm_genome_stub.c
)

target_include_directories(kolibri_wasm
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backend/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(kolibri_wasm PUBLIC kolibri_core)

add_executable(kolibri_node apps/kolibri_node.c)
add_executable(ks_compiler apps/ks_compiler.c)
add_executable(kolibri_knowledge_server backend/src/knowledge_server.c)
add_executable(kolibri_indexer apps/kolibri_indexer.c)
add_executable(kolibri_queue apps/kolibri_queue.c)
add_executable(kolibri_sim apps/kolibri_sim_cli.c)
add_executable(kolibri_coordinator apps/kolibri_coordinator.c)
add_executable(kolibri_knowledge_relay apps/kolibri_knowledge_relay.c)
add_executable(kolibri_verify_digit_masking tools/verify_digit_masking.c)
add_executable(kolibri_archiver apps/kolibri_archiver.c)
add_executable(kolibri_formula_archiver tools/kolibri_formula_archiver.c)
add_executable(kolibri_meta_archiver tools/kolibri_meta_archiver.c)

target_link_libraries(kolibri_node PRIVATE kolibri_core)
target_link_libraries(ks_compiler PRIVATE kolibri_core)
target_link_libraries(kolibri_knowledge_server PRIVATE kolibri_core)
target_link_libraries(kolibri_indexer PRIVATE kolibri_core)
target_link_libraries(kolibri_queue PRIVATE kolibri_core)
target_link_libraries(kolibri_sim PRIVATE kolibri_core)
target_link_libraries(kolibri_coordinator PRIVATE kolibri_core)
target_link_libraries(kolibri_knowledge_relay PRIVATE kolibri_core)
target_link_libraries(kolibri_verify_digit_masking PRIVATE kolibri_core)
target_link_libraries(kolibri_archiver PRIVATE kolibri_core)
target_link_libraries(kolibri_formula_archiver PRIVATE kolibri_core)
if(KOLIBRI_ENABLE_GPU)
    target_link_libraries(kolibri_formula_archiver PRIVATE kolibri_gpu)
endif()
target_link_libraries(kolibri_meta_archiver PRIVATE kolibri_core)

add_custom_target(verify_digit_masking ALL
    COMMAND $<TARGET_FILE:kolibri_verify_digit_masking>
    DEPENDS kolibri_verify_digit_masking)

if(KOLIBRI_ENABLE_TESTS)
    enable_testing()
    add_executable(kolibri_tests
        tests/test_main.c
        tests/test_decimal.c
        tests/test_genome.c
        tests/test_formula.c
        tests/test_roy.c
        tests/test_script.c
        tests/test_net.c
        tests/test_knowledge_index.c
        tests/test_knowledge_queue.c
        tests/test_sim.c
        tests/test_public_api.c
    )
    target_link_libraries(kolibri_tests PRIVATE kolibri_core Threads::Threads)
    add_test(NAME kolibri_tests COMMAND kolibri_tests)
    
    # AGI v2.0 Phase 1: Semantic module tests
    add_executable(test_semantic tests/test_semantic.c)
    target_link_libraries(test_semantic PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_semantic COMMAND test_semantic)
    
    # AGI v2.0 Phase 1.2: Context window tests
    add_executable(test_context tests/test_context.c)
    target_link_libraries(test_context PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_context COMMAND test_context)
    
    # AGI v2.0 Phase 1.3: Corpus learning tests
    add_executable(test_corpus tests/test_corpus.c)
    target_link_libraries(test_corpus PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_corpus COMMAND test_corpus)
    
    # AGI v2.0 Phase 2: Text generation tests
    add_executable(test_generation tests/test_generation.c)
    target_link_libraries(test_generation PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_generation COMMAND test_generation)
    
    # Kolibri Archiver: Compression tests
    add_executable(test_compress tests/test_compress.c)
    target_link_libraries(test_compress PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_compress COMMAND test_compress)
    # MEGA COMPRESSION TEST - демонстрация 300000x изобретения!
    add_executable(test_mega_compression tests/test_mega_compression.c)
    target_link_libraries(test_mega_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_mega_compression COMMAND test_mega_compression)
    
    # EXTREME COMPRESSION TEST - 20 МБ тест!
    add_executable(test_extreme_compression tests/test_extreme_compression.c)
    target_link_libraries(test_extreme_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_extreme_compression COMMAND test_extreme_compression)
    
    # MULTILEVEL COMPRESSION TEST - путь к 300000x!
    add_executable(test_multilevel_compression tests/test_multilevel_compression.c)
    target_link_libraries(test_multilevel_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_multilevel_compression COMMAND test_multilevel_compression)
    
    # MULTILEVEL DEMO - демонстрация архитектуры 300000x!
    add_executable(test_multilevel_demo tests/test_multilevel_demo.c)
    target_link_libraries(test_multilevel_demo PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_multilevel_demo COMMAND test_multilevel_demo)
    
    # REAL FILE COMPRESSION - тест на реальном файле!
    add_executable(test_real_file_compression tests/test_real_file_compression.c)
    target_link_libraries(test_real_file_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_real_file_compression COMMAND test_real_file_compression)
    
    # COMPRESS PROJECT - сжатие всего проекта!
    add_executable(test_compress_project tests/test_compress_project.c)
    target_link_libraries(test_compress_project PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_compress_project COMMAND test_compress_project)
    
    # FULL PROJECT COMPRESSION - сжатие всего проекта (рабочая версия)!
    add_executable(test_full_project_compression tests/test_full_project_compression.c)
    target_link_libraries(test_full_project_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_full_project_compression COMMAND test_full_project_compression)
    
    # PROJECT ARCHIVE - создание полного архива проекта!
    add_executable(test_project_archive tests/test_project_archive.c)
    target_link_libraries(test_project_archive PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_project_archive COMMAND test_project_archive)
    
    # MULTILEVEL ARCHIVE - многоуровневое сжатие архива!
    add_executable(test_multilevel_archive tests/test_multilevel_archive.c)
    target_link_libraries(test_multilevel_archive PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_multilevel_archive COMMAND test_multilevel_archive)
    
    # PROOF 300000x - доказательство компрессии 300,000x!
    add_executable(test_proof_300000x tests/test_proof_300000x.c)
    target_link_libraries(test_proof_300000x PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_proof_300000x COMMAND test_proof_300000x)
    
    # IMPLEMENT 300000x - реализация компрессии 300,000x!
    add_executable(test_implement_300000x tests/test_implement_300000x.c)
    target_link_libraries(test_implement_300000x PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_implement_300000x COMMAND test_implement_300000x)

    # SUPER ARCHIVE - финальный супер-архив проекта с 300,000x+
    add_executable(test_super_archive tests/test_super_archive.c)
    target_link_libraries(test_super_archive PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_super_archive COMMAND test_super_archive)

    # RESTORE SUPER ARCHIVE - восстановление из супер-архива
    add_executable(test_restore_super_archive tests/test_restore_super_archive.c)
    target_link_libraries(test_restore_super_archive PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_restore_super_archive COMMAND test_restore_super_archive)

    # RESTORE PROJECT - полное восстановление проекта
    add_executable(test_restore_project tests/test_restore_project.c)
    target_link_libraries(test_restore_project PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_restore_project COMMAND test_restore_project)

    # ARCHIVE RESTORE DEMO - демонстрация восстановления (как с изображением)
    add_executable(test_archive_restore_demo tests/test_archive_restore_demo.c)
    target_link_libraries(test_archive_restore_demo PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_archive_restore_demo COMMAND test_archive_restore_demo)

    # CORRECTNESS TESTS - проверка корректности кодирования
    add_executable(test_correctness tests/test_correctness.c)
    target_link_libraries(test_correctness PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_correctness COMMAND test_correctness)

    # STRESS TESTS - стресс-тестирование многопоточности
    add_executable(test_stress tests/test_stress.c)
    target_link_libraries(test_stress PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_stress COMMAND test_stress --quick)

    configure_file(tests/ks_compiler_roundtrip.cmake
                   ${CMAKE_CURRENT_BINARY_DIR}/ks_compiler_roundtrip.cmake
                   @ONLY)
    add_test(NAME ks_compiler_roundtrip
             COMMAND ${CMAKE_COMMAND}
                     -Dks_compiler=$<TARGET_FILE:ks_compiler>
                     -P ${CMAKE_CURRENT_BINARY_DIR}/ks_compiler_roundtrip.cmake)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME kolibri_node_custom_key_inline
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_kolibri_node_hmac.py
                         $<TARGET_FILE:kolibri_node>
                         inline)
        add_test(NAME kolibri_node_custom_key_file
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_kolibri_node_hmac.py
                         $<TARGET_FILE:kolibri_node>
                         file)
    endif()

    add_test(NAME kolibri_node_usage COMMAND $<TARGET_FILE:kolibri_node> --help)

    add_test(NAME kolibri_indexer_usage COMMAND $<TARGET_FILE:kolibri_indexer>)
    set_tests_properties(kolibri_indexer_usage PROPERTIES WILL_FAIL TRUE)

    add_test(NAME kolibri_queue_usage COMMAND $<TARGET_FILE:kolibri_queue>)
    set_tests_properties(kolibri_queue_usage PROPERTIES WILL_FAIL TRUE)

    add_test(NAME kolibri_sim_usage COMMAND $<TARGET_FILE:kolibri_sim>)
    set_tests_properties(kolibri_sim_usage PROPERTIES WILL_FAIL TRUE)
endif()

# (опционально) добавим минимальные тесты по digits
if(TARGET kolibri_tests)
  target_sources(kolibri_tests PRIVATE tests/test_digits.c)
endif()

# Benchmark executables (not part of ctest, run manually)
add_executable(kolibri_benchmark_suite benchmarks/kolibri_benchmark_suite.c)
target_include_directories(kolibri_benchmark_suite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backend/include)
target_link_libraries(kolibri_benchmark_suite PRIVATE m)

add_executable(compare_with_competitors benchmarks/compare_with_competitors.c)
target_include_directories(compare_with_competitors PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backend/include)
target_link_libraries(compare_with_competitors PRIVATE m)

if(KOLIBRI_ENABLE_FUZZ)
  add_executable(kolibri_fuzz_script tests/fuzz_script.c)
  target_link_libraries(kolibri_fuzz_script PRIVATE kolibri_core)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(kolibri_fuzz_script PRIVATE -fsanitize=fuzzer,address)
    target_link_options(kolibri_fuzz_script PRIVATE -fsanitize=fuzzer,address)
  else()
    message(WARNING "Kolibri fuzz targets are best built with Clang/libFuzzer")
  endif()
endif()
