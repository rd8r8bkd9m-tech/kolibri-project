cmake_minimum_required(VERSION 3.16)
project(kolibri LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(KOLIBRI_ENABLE_TESTS "Enable Kolibri tests" ON)
option(KOLIBRI_ENABLE_FUZZ "Enable Kolibri fuzz targets" OFF)
option(KOLIBRI_ENABLE_WASM_TARGET "Enable kolibri.wasm custom target" ON)
option(KOLIBRI_WASM_INCLUDE_GENOME "Include persistent genome into kolibri.wasm" OFF)
option(KOLIBRI_WASM_GENERATE_MAP "Emit symbol map for kolibri.wasm" OFF)

set(KOLIBRI_WASM_EMCC "" CACHE STRING "Override emcc executable for kolibri.wasm builds")
set(KOLIBRI_WASM_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build/wasm" CACHE PATH "Output directory for kolibri.wasm artifact")

add_library(kolibri_core_objects OBJECT
    backend/src/decimal.c
    backend/src/digits.c
    backend/src/digit_text.c
    backend/src/genome.c
    backend/src/sha256.c
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
    backend/src/logical_memory.c
    backend/src/formula_logic.c
    kernel/ai_encoder.c
    kernel/ai_evolution.c
)

target_include_directories(kolibri_core_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backend/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kernel>
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
target_link_libraries(kolibri_core PUBLIC ${KOLIBRI_OPENSSL_TARGET} SQLite::SQLite3)

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

if(KOLIBRI_ENABLE_WASM_TARGET)
    set(KOLIBRI_WASM_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/decimal.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/digits.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/formula.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/genome.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/sha256.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/random.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/symbol_table.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/script.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/sim.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/wasm_bridge.c
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/src/wasm_genome_stub.c
        ${CMAKE_CURRENT_SOURCE_DIR}/wasm/kolibri_sim_wasm.c
    )

    set(_kolibri_wasm_env
        "KOLIBRI_WASM_BUILD_FROM_CMAKE=1"
        "KOLIBRI_WASM_OUTPUT_DIR=${KOLIBRI_WASM_OUTPUT_DIR}"
    )

    if(KOLIBRI_WASM_EMCC)
        list(APPEND _kolibri_wasm_env "EMCC=${KOLIBRI_WASM_EMCC}")
    endif()
    if(KOLIBRI_WASM_INCLUDE_GENOME)
        list(APPEND _kolibri_wasm_env "KOLIBRI_WASM_INCLUDE_GENOME=1")
    endif()
    if(KOLIBRI_WASM_GENERATE_MAP)
        list(APPEND _kolibri_wasm_env "KOLIBRI_WASM_GENERATE_MAP=1")
    endif()

    set(KOLIBRI_WASM_MODULE "${KOLIBRI_WASM_OUTPUT_DIR}/kolibri.wasm")
    set(KOLIBRI_WASM_INFO "${KOLIBRI_WASM_OUTPUT_DIR}/kolibri.wasm.txt")

    add_custom_command(
        OUTPUT ${KOLIBRI_WASM_MODULE} ${KOLIBRI_WASM_INFO}
        COMMAND ${CMAKE_COMMAND} -E env ${_kolibri_wasm_env} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_wasm.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS ${KOLIBRI_WASM_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_wasm.sh
        BYPRODUCTS
            ${KOLIBRI_WASM_OUTPUT_DIR}/kolibri.wasm.sha256
            ${KOLIBRI_WASM_OUTPUT_DIR}/kolibri.wasm.report.json
        COMMENT "Сборка WebAssembly ядра Kolibri (kolibri.wasm)"
        VERBATIM
    )

    add_custom_target(kolibri_wasm_artifact DEPENDS ${KOLIBRI_WASM_MODULE} ${KOLIBRI_WASM_INFO})
endif()

add_executable(kolibri_node apps/kolibri_node.c)
add_executable(ks_compiler apps/ks_compiler.c)
add_executable(kolibri_knowledge_server backend/src/knowledge_server.c)
add_executable(kolibri_indexer apps/kolibri_indexer.c)
add_executable(kolibri_queue apps/kolibri_queue.c)
add_executable(kolibri_sim apps/kolibri_sim_cli.c)
add_executable(kolibri_coordinator apps/kolibri_coordinator.c)
add_executable(kolibri_knowledge_relay apps/kolibri_knowledge_relay.c)
add_executable(kolibri_verify_digit_masking tools/verify_digit_masking.c)

target_link_libraries(kolibri_node PRIVATE kolibri_core)
target_link_libraries(ks_compiler PRIVATE kolibri_core)
target_link_libraries(kolibri_knowledge_server PRIVATE kolibri_core)
target_link_libraries(kolibri_indexer PRIVATE kolibri_core)
target_link_libraries(kolibri_queue PRIVATE kolibri_core)
target_link_libraries(kolibri_sim PRIVATE kolibri_core)
target_link_libraries(kolibri_coordinator PRIVATE kolibri_core)
target_link_libraries(kolibri_knowledge_relay PRIVATE kolibri_core)
target_link_libraries(kolibri_verify_digit_masking PRIVATE kolibri_core)

add_custom_command(TARGET kolibri_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:kolibri_sim>
            ${CMAKE_BINARY_DIR}/kolibri_sim_cli${CMAKE_EXECUTABLE_SUFFIX}
    COMMENT "Creating kolibri_sim_cli alias binary for compatibility"
)

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
    
    # AI Encoder tests (optimized decimal encoding for AI)
    add_executable(test_ai_encoder tests/test_ai_encoder.c)
    target_link_libraries(test_ai_encoder PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_ai_encoder PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_ai_encoder COMMAND test_ai_encoder)

    # Text roundtrip test (real-world UTF-8 → Decimal → AI → Decode)
    add_executable(test_text_roundtrip tests/test_text_roundtrip.c)
    target_link_libraries(test_text_roundtrip PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_text_roundtrip PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_text_roundtrip COMMAND test_text_roundtrip)

    # Storage demo test (prove lossless encoding allows deleting originals)
    add_executable(test_storage_demo tests/test_storage_demo.c)
    target_link_libraries(test_storage_demo PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_storage_demo PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_storage_demo COMMAND test_storage_demo)

    # Formula compression test (pattern-based compression vs naive double encoding)
    add_executable(test_formula_compression tests/test_formula_compression.c)
    target_link_libraries(test_formula_compression PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_formula_compression PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_formula_compression COMMAND test_formula_compression)

    # Formula-only storage test (prove formulas can replace original data)
    add_executable(test_formula_storage tests/test_formula_storage.c)
    target_link_libraries(test_formula_storage PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_formula_storage PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_formula_storage COMMAND test_formula_storage)

    # Logical memory test (memory without data - pure logic)
    add_executable(test_logical_memory tests/test_logical_memory.c)
    target_link_libraries(test_logical_memory PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_logical_memory PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_logical_memory COMMAND test_logical_memory)

    # Formula logic test (meta-formulas: formulas that generate logic)
    add_executable(test_formula_logic tests/test_formula_logic.c)
    target_link_libraries(test_formula_logic PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_formula_logic PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_formula_logic COMMAND test_formula_logic)

    # Full pipeline test (source -> encode -> logic -> meta -> reconstruct -> verify)
    add_executable(test_full_pipeline tests/test_full_pipeline.c)
    target_link_libraries(test_full_pipeline PRIVATE kolibri_core Threads::Threads)
    target_include_directories(test_full_pipeline PRIVATE 
        ${CMAKE_CURRENT_SOURCE_DIR}/backend/include
        ${CMAKE_CURRENT_SOURCE_DIR}/kernel)
    add_test(NAME test_full_pipeline COMMAND test_full_pipeline)

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
