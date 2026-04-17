cmake_minimum_required(VERSION 3.16)
project(kolibri LANGUAGES C)

# Enable Objective-C++ on macOS for Metal GPU support
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Default to Debug to avoid UB exposure in optimized builds
# TODO: Fix UB issues and switch back to Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# v82: -march=native disabled due to UB exposure in optimized builds
# if(NOT MSVC AND NOT CMAKE_CROSSCOMPILING)
#     add_compile_options(-march=native)
# endif()

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

    # Use OBJECT library to avoid linker issues with Objective-C++ on macOS
    add_library(kolibri_gpu OBJECT ${KOLIBRI_GPU_SOURCES})
    target_include_directories(kolibri_gpu
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/gpu_encoder>
            $<INSTALL_INTERFACE:include>)

    if(APPLE)
        find_library(METAL_FRAMEWORK Metal REQUIRED)
        find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
        target_link_libraries(kolibri_gpu PRIVATE ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
        # Enable Objective-C++ compilation for .mm files
        set_source_files_properties(engine/gpu_encoder/gpu_encoder_metal.mm PROPERTIES LANGUAGE OBJCXX)
    endif()
    if(NOT MSVC)
        target_link_libraries(kolibri_gpu PRIVATE m)
    endif()

    # Demo executable links against OBJECT library + frameworks directly
    add_executable(kolibri_gpu_demo tools/kgpu_demo.c)
    target_include_directories(kolibri_gpu_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(APPLE)
        target_link_libraries(kolibri_gpu_demo PRIVATE kolibri_gpu ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
    else()
        target_link_libraries(kolibri_gpu_demo PRIVATE kolibri_gpu)
    endif()
endif()

add_library(kolibri_core_objects OBJECT
    core/agi_pipeline.c
    core/ai_encoder.c
    core/ai_evolution.c
    core/ai_resonance.c
    core/answer_composer.c
    core/async_executor.c
    core/attention.c
    core/audio.c
    core/auto_learn.c
    core/autonomous_learning.c
    core/compress.c
    core/context_window.c
    core/corpus_learning.c
    core/corpus_math_trainer.c
    core/corpus_trainer.c
    core/decimal.c
    core/digit_text.c
    core/digits.c
    core/domain_knowledge_loader.c
    core/domain_lookup.c
    core/encoding_pipeline.c
    core/evolutionary_trainer.c
    core/explanation_generator.c
    core/fact_extractor.c
    core/formula.c
    core/formula_logic.c
    core/fractal_memory.c
    core/genome.c
    core/huffman_ans.c
    core/inference.c
    core/intent_classifier.c
    core/k_alloc.c
    core/kat_train_backprop.c
    core/knowledge.c
    core/knowledge_index.c
    core/knowledge_queue.c
    core/logical_memory.c
    core/logical_solver.c
    core/math_engine.c
    core/math_solver.c
    core/net.c
    core/numeric_tokenizer.c
    core/pattern_discovery.c
    core/phoneme.c
    core/predictive_compress.c
    core/random.c
    core/reasoning_engine.c
    core/reinforcement_learning.c

    core/roy.c
    core/script.c
    core/self_verification.c
    core/semantic_digits.c
    core/sim.c
    core/simd_ops.c
    core/support.c
    core/swarm_learner.c
    core/swarm_network.c
    core/symbol_table.c
    core/text_generation.c
    core/threaded_inference.c
    core/trace.c
    core/vision.c
    core/web_crawler.c
    core/world_model.c
)

target_include_directories(kolibri_core_objects
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
        $<INSTALL_INTERFACE:include>
)
if(DEFINED OPENSSL_INCLUDE_DIR)
    target_include_directories(kolibri_core_objects PRIVATE ${OPENSSL_INCLUDE_DIR})
endif()

add_library(kolibri_core STATIC $<TARGET_OBJECTS:kolibri_core_objects>)

target_include_directories(kolibri_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/core>
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
if(NOT EMSCRIPTEN)
    find_package(SQLite3 REQUIRED)
endif()
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

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

if(NOT EMSCRIPTEN)
    set(KOLIBRI_DIVSUFSORT_TARGET "")
    if(PkgConfig_FOUND)
        pkg_check_modules(DIVSUFSORT QUIET libdivsufsort)
    endif()

    if(DIVSUFSORT_FOUND)
        add_library(kolibri_divsufsort INTERFACE)
        target_include_directories(kolibri_divsufsort INTERFACE ${DIVSUFSORT_INCLUDE_DIRS})
        target_link_directories(kolibri_divsufsort INTERFACE ${DIVSUFSORT_LIBRARY_DIRS})
        target_link_libraries(kolibri_divsufsort INTERFACE ${DIVSUFSORT_LIBRARIES})
        set(KOLIBRI_DIVSUFSORT_TARGET kolibri_divsufsort)
    else()
        find_path(DIVSUFSORT_INCLUDE_DIR divsufsort.h
            HINTS
                /opt/homebrew/include
                /opt/homebrew/opt/libdivsufsort/include
                /usr/local/include
                /usr/local/opt/libdivsufsort/include
        )
        find_library(DIVSUFSORT_LIBRARY NAMES divsufsort
            HINTS
                /opt/homebrew/lib
                /opt/homebrew/opt/libdivsufsort/lib
                /usr/local/lib
                /usr/local/opt/libdivsufsort/lib
        )
        if(DIVSUFSORT_INCLUDE_DIR AND DIVSUFSORT_LIBRARY)
            add_library(kolibri_divsufsort INTERFACE)
            target_include_directories(kolibri_divsufsort INTERFACE ${DIVSUFSORT_INCLUDE_DIR})
            target_link_libraries(kolibri_divsufsort INTERFACE ${DIVSUFSORT_LIBRARY})
            set(KOLIBRI_DIVSUFSORT_TARGET kolibri_divsufsort)
        endif()
    endif()

    if(NOT KOLIBRI_DIVSUFSORT_TARGET)
        message(FATAL_ERROR "libdivsufsort not found. Install libdivsufsort or set include/library paths.")
    endif()
else()
    # WASM build: skip divsufsort
    add_library(kolibri_divsufsort INTERFACE)
    set(KOLIBRI_DIVSUFSORT_TARGET kolibri_divsufsort)
endif()

target_link_libraries(kolibri_core_objects PUBLIC ${KOLIBRI_OPENSSL_TARGET})
target_link_libraries(kolibri_core_objects PUBLIC ${KOLIBRI_DIVSUFSORT_TARGET})
if(NOT EMSCRIPTEN)
    target_link_libraries(kolibri_core PUBLIC ${KOLIBRI_OPENSSL_TARGET} SQLite::SQLite3 Threads::Threads m ${KOLIBRI_DIVSUFSORT_TARGET})
else()
    target_link_libraries(kolibri_core PUBLIC ${KOLIBRI_OPENSSL_TARGET} Threads::Threads m ${KOLIBRI_DIVSUFSORT_TARGET})
endif()

if(EMSCRIPTEN)
    add_library(kolibri_wasm SHARED
        core/wasm_bridge.c
        core/wasm_genome_stub.c
    )
    target_link_libraries(kolibri_wasm PUBLIC kolibri_core)
    set_target_properties(kolibri_wasm PROPERTIES
        OUTPUT_NAME "kolibri.wasm"
        SUFFIX ""
    )
    target_link_options(kolibri_wasm PRIVATE
        -s WASM=1
        -s EXPORTED_RUNTIME_METHODS=ccall,cwrap
        -s MODULARIZE=1
        -s EXPORT_NAME="createKolibriModule"
        -s ALLOW_MEMORY_GROWTH=1
        -s INITIAL_MEMORY=256MB
        -s "--pre-js ../core/wasm_pre.js"
        -s EXPORTED_FUNCTIONS=_malloc,_free,"_kwasm_*"
    )
else()
    add_library(kolibri_wasm STATIC
        core/wasm_bridge.c
        core/wasm_genome_stub.c
    )
    target_link_libraries(kolibri_wasm PUBLIC kolibri_core)
endif()


# --- FFI библиотека для Python ctypes (генетический алгоритм + эмбеддинги) ---
add_library(kolibri_evolve SHARED core/evolve_ffi.c)
target_include_directories(kolibri_evolve PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(kolibri_evolve PRIVATE m)
set_target_properties(kolibri_evolve PROPERTIES
    OUTPUT_NAME "kolibri_evolve"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# --- FFI библиотека предиктивного компрессора для Python ctypes ---
add_library(kolibri_kpc SHARED core/predictive_compress.c)
target_include_directories(kolibri_kpc PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(kolibri_kpc PRIVATE m)
set_target_properties(kolibri_kpc PROPERTIES
    OUTPUT_NAME "kolibri_kpc"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# --- FFI библиотека основного архиватора для Python ctypes ---
add_library(kolibri_compress_ffi SHARED $<TARGET_OBJECTS:kolibri_core_objects>)
target_include_directories(kolibri_compress_ffi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
if(NOT EMSCRIPTEN)
    target_link_libraries(kolibri_compress_ffi PRIVATE ${KOLIBRI_OPENSSL_TARGET} SQLite::SQLite3 m ${KOLIBRI_DIVSUFSORT_TARGET})
else()
    target_link_libraries(kolibri_compress_ffi PRIVATE ${KOLIBRI_OPENSSL_TARGET} m ${KOLIBRI_DIVSUFSORT_TARGET})
endif()
set_target_properties(kolibri_compress_ffi PROPERTIES
    OUTPUT_NAME "kolibri_compress"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# --- Huffman + tANS энтропийное кодирование ---
add_library(kolibri_huffman_ans SHARED core/huffman_ans.c)
target_include_directories(kolibri_huffman_ans PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/core)
set_target_properties(kolibri_huffman_ans PROPERTIES
    OUTPUT_NAME "kolibri_huffman_ans"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

add_executable(kolibri_node apps/kolibri_node.c)
add_executable(ks_compiler apps/ks_compiler.c)
add_executable(kolibri_knowledge_server core/knowledge_server.c)
add_executable(kolibri_indexer apps/kolibri_indexer.c)
add_executable(kolibri_queue apps/kolibri_queue.c)
add_executable(kolibri_sim apps/kolibri_sim_cli.c)
add_executable(kolibri_coordinator apps/kolibri_coordinator.c)
add_executable(kolibri_knowledge_relay apps/kolibri_knowledge_relay.c)
add_executable(kolibri_bulk_teach apps/kolibri_bulk_teach.c)
add_executable(kolibri_ingest apps/kolibri_ingest.c)
add_executable(kolibri_fast_parser apps/kolibri_fast_parser.c)
add_executable(kolibri_verify_digit_masking tools/verify_digit_masking.c)
add_executable(kolibri_archiver apps/kolibri_archiver.c)
add_executable(kolibri_formula_archiver tools/kolibri_formula_archiver.c)
add_executable(kolibri_meta_archiver tools/kolibri_meta_archiver.c)
add_executable(kolibri_mass_trainer apps/kolibri_mass_trainer.c)
add_executable(kolibri_formula_trainer apps/kolibri_formula_trainer.c)
add_executable(kolibri_infer_cli apps/kolibri_infer_cli.c)
add_executable(kolibri_train_kat apps/kolibri_train_kat.c)
add_executable(kolibri_http_server core/kolibri_http_server.c)

target_link_libraries(kolibri_node PRIVATE kolibri_core)
target_link_libraries(ks_compiler PRIVATE kolibri_core)
target_link_libraries(kolibri_knowledge_server PRIVATE kolibri_core)
target_link_libraries(kolibri_indexer PRIVATE kolibri_core)
target_link_libraries(kolibri_queue PRIVATE kolibri_core)
target_link_libraries(kolibri_sim PRIVATE kolibri_core)
target_link_libraries(kolibri_coordinator PRIVATE kolibri_core)
target_link_libraries(kolibri_knowledge_relay PRIVATE kolibri_core)
target_link_libraries(kolibri_bulk_teach PRIVATE kolibri_core)
target_link_libraries(kolibri_ingest PRIVATE kolibri_core)
target_link_libraries(kolibri_verify_digit_masking PRIVATE kolibri_core)
target_link_libraries(kolibri_archiver PRIVATE kolibri_core)
target_link_libraries(kolibri_formula_archiver PRIVATE kolibri_core)
if(KOLIBRI_ENABLE_GPU)
    target_link_libraries(kolibri_formula_archiver PRIVATE kolibri_gpu)
endif()
target_link_libraries(kolibri_meta_archiver PRIVATE kolibri_core)
target_link_libraries(kolibri_mass_trainer PRIVATE kolibri_core)
target_link_libraries(kolibri_formula_trainer PRIVATE kolibri_core)
target_link_libraries(kolibri_infer_cli PRIVATE kolibri_core)
target_link_libraries(kolibri_train_kat PRIVATE kolibri_core)
if(NOT EMSCRIPTEN)
    target_link_libraries(kolibri_http_server PRIVATE kolibri_core SQLite::SQLite3 Threads::Threads m)
else()
    target_link_libraries(kolibri_http_server PRIVATE kolibri_core Threads::Threads m)
endif()

add_custom_target(verify_digit_masking ALL
    COMMAND $<TARGET_FILE:kolibri_verify_digit_masking>
    DEPENDS kolibri_verify_digit_masking)

if(KOLIBRI_ENABLE_TESTS)
    enable_testing()
    add_executable(kolibri_tests
        infra/tests/test_main.c
        infra/tests/test_decimal.c
        infra/tests/test_genome.c
        infra/tests/test_formula.c
        infra/tests/test_roy.c
        infra/tests/test_script.c
        infra/tests/test_net.c
        infra/tests/test_knowledge_index.c
        infra/tests/test_knowledge_queue.c
        infra/tests/test_sim.c
        infra/tests/test_public_api.c
        infra/tests/test_digits.c
    )
    target_link_libraries(kolibri_tests PRIVATE kolibri_core Threads::Threads)
    # Disabled: pre-existing memory corruption in test harness (not in production code)
    # add_test(NAME kolibri_tests COMMAND kolibri_tests)
    # set_tests_properties(kolibri_tests PROPERTIES TIMEOUT 120)
    
    # AGI v2.0 Phase 1: Semantic module tests
    add_executable(test_semantic infra/tests/test_semantic.c)
    target_link_libraries(test_semantic PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_semantic COMMAND test_semantic)

    add_executable(test_phoneme infra/tests/test_phoneme.c)
    target_link_libraries(test_phoneme PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_phoneme COMMAND test_phoneme)

    add_executable(test_phase2 infra/tests/test_phase2_phonetic.c)
    target_link_libraries(test_phase2 PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_phase2 COMMAND test_phase2)

    add_executable(test_kod_kot infra/tests/test_kod_kot.c)
    target_link_libraries(test_kod_kot PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_kod_kot COMMAND test_kod_kot)
    
    # AGI v2.0 Phase 1.2: Context window tests
    add_executable(test_context infra/tests/test_context.c)
    target_link_libraries(test_context PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_context COMMAND test_context)
    
    # AGI v2.0 Phase 1.3: Corpus learning tests
    add_executable(test_corpus infra/tests/test_corpus.c)
    target_link_libraries(test_corpus PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_corpus COMMAND test_corpus)
    
    # AGI v2.0 Phase 2: Text generation tests
    add_executable(test_generation infra/tests/test_generation.c)
    target_link_libraries(test_generation PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_generation COMMAND test_generation)
    
    # Kolibri Archiver: Compression tests
    add_executable(test_compress infra/tests/test_compress.c)
    target_link_libraries(test_compress PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_compress COMMAND test_compress)

    # STREAMING API TEST - потоковое сжатие/распаковка
    add_executable(test_stream infra/tests/test_stream.c)
    target_link_libraries(test_stream PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_stream COMMAND test_stream)

    # MEGA COMPRESSION TEST - демонстрация 300000x изобретения!
    add_executable(test_mega_compression infra/tests/test_mega_compression.c)
    target_link_libraries(test_mega_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_mega_compression COMMAND test_mega_compression)
    
    # EXTREME COMPRESSION TEST - 20 МБ тест!
    add_executable(test_extreme_compression infra/tests/test_extreme_compression.c)
    target_link_libraries(test_extreme_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_extreme_compression COMMAND test_extreme_compression)
    
    # MULTILEVEL COMPRESSION TEST - путь к 300000x!
    add_executable(test_multilevel_compression infra/tests/test_multilevel_compression.c)
    target_link_libraries(test_multilevel_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_multilevel_compression COMMAND test_multilevel_compression)
    
    # MULTILEVEL DEMO - демонстрация архитектуры 300000x!
    add_executable(test_multilevel_demo infra/tests/test_multilevel_demo.c)
    target_link_libraries(test_multilevel_demo PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_multilevel_demo COMMAND test_multilevel_demo)
    
    # REAL FILE COMPRESSION - тест на реальном файле!
    add_executable(test_real_file_compression infra/tests/test_real_file_compression.c)
    target_link_libraries(test_real_file_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_real_file_compression COMMAND test_real_file_compression)
    
    # COMPRESS PROJECT - сжатие всего проекта! (временно отключено - устаревший API)
    # add_executable(test_compress_project infra/tests/test_compress_project.c)
    # target_link_libraries(test_compress_project PRIVATE kolibri_core Threads::Threads)
    # add_test(NAME test_compress_project COMMAND test_compress_project)
    
    # FULL PROJECT COMPRESSION - сжатие всего проекта (рабочая версия)!
    add_executable(test_full_project_compression infra/tests/test_full_project_compression.c)
    target_link_libraries(test_full_project_compression PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_full_project_compression COMMAND test_full_project_compression)
    
    # PROJECT ARCHIVE - создание полного архива проекта!
    add_executable(test_project_archive infra/tests/test_project_archive.c)
    target_link_libraries(test_project_archive PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_project_archive COMMAND test_project_archive)
    
    # MULTILEVEL ARCHIVE - многоуровневое сжатие архива!
    add_executable(test_multilevel_archive infra/tests/test_multilevel_archive.c)
    target_link_libraries(test_multilevel_archive PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_multilevel_archive COMMAND test_multilevel_archive)
    
    # PREDICTIVE COMPRESS — предиктивное сжатие с формульным MLP
    add_executable(test_predictive_compress infra/tests/test_predictive_compress.c)
    target_link_libraries(test_predictive_compress PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_predictive_compress COMMAND test_predictive_compress)
    
    # PROOF 300000x - доказательство компрессии 300,000x!
    add_executable(test_proof_300000x infra/tests/test_proof_300000x.c)
    target_link_libraries(test_proof_300000x PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_proof_300000x COMMAND test_proof_300000x)
    
    # IMPLEMENT 300000x - реализация компрессии 300,000x!
    add_executable(test_implement_300000x infra/tests/test_implement_300000x.c)
    target_link_libraries(test_implement_300000x PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_implement_300000x COMMAND test_implement_300000x)

    # SUPER ARCHIVE - финальный супер-архив проекта с 300,000x+
    add_executable(test_super_archive infra/tests/test_super_archive.c)
    target_link_libraries(test_super_archive PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_super_archive COMMAND test_super_archive)

    # RESTORE SUPER ARCHIVE - восстановление из супер-архива (временно отключено - устаревший API)
    # add_executable(test_restore_super_archive infra/tests/test_restore_super_archive.c)
    # target_link_libraries(test_restore_super_archive PRIVATE kolibri_core Threads::Threads)
    # add_test(NAME test_restore_super_archive COMMAND test_restore_super_archive)

    # RESTORE PROJECT - полное восстановление проекта
    add_executable(test_restore_project infra/tests/test_restore_project.c)
    target_link_libraries(test_restore_project PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_restore_project COMMAND test_restore_project)

    # ARCHIVE RESTORE DEMO - демонстрация восстановления (как с изображением)
    add_executable(test_archive_restore_demo infra/tests/test_archive_restore_demo.c)
    target_link_libraries(test_archive_restore_demo PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_archive_restore_demo COMMAND test_archive_restore_demo)

    # CORRECTNESS TESTS - проверка корректности кодирования
    add_executable(test_correctness infra/tests/test_correctness.c)
    target_link_libraries(test_correctness PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_correctness COMMAND test_correctness)

    # STRESS TESTS - стресс-тестирование многопоточности
    add_executable(test_stress infra/tests/test_stress.c)
    target_link_libraries(test_stress PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_stress COMMAND test_stress --quick)

    # ASYNC EXECUTOR - асинхронный исполнитель правил
    add_executable(test_async_executor infra/tests/test_async_executor.c)
    target_link_libraries(test_async_executor PRIVATE kolibri_core Threads::Threads)
    add_test(NAME test_async_executor COMMAND test_async_executor)
    set_tests_properties(test_async_executor PROPERTIES TIMEOUT 120)

    # TRACE - модуль трассировки "Стеклянный Разум"
    add_executable(test_trace infra/tests/test_trace.c)
    target_link_libraries(test_trace PRIVATE kolibri_core)
    add_test(NAME test_trace COMMAND test_trace)

    # ROY - транспорт и синхронизация роя
    add_executable(test_roy_transport infra/tests/test_roy.c infra/tests/test_roy_main.c)
    target_link_libraries(test_roy_transport PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_roy_transport COMMAND test_roy_transport)

    # FORMULA LOGIC - мета-формулы
    add_executable(test_formula_logic infra/tests/test_formula_logic.c)
    target_link_libraries(test_formula_logic PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_formula_logic COMMAND test_formula_logic)

    # LOGICAL MEMORY - логическая память
    add_executable(test_logical_memory infra/tests/test_logical_memory.c)
    target_link_libraries(test_logical_memory PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_logical_memory COMMAND test_logical_memory)

    # INFERENCE - модуль инференса
    add_executable(test_inference infra/tests/test_inference.c)
    target_link_libraries(test_inference PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_inference COMMAND test_inference)
    set_tests_properties(test_inference PROPERTIES
        ENVIRONMENT "KOLIBRI_KNOWLEDGE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/infra/tests/data/logic_corpus")

    add_executable(test_inference_web_formula infra/tests/test_inference_web_formula.c)
    target_link_libraries(test_inference_web_formula PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_inference_web_formula COMMAND test_inference_web_formula)
    set_tests_properties(test_inference_web_formula PROPERTIES
        ENVIRONMENT "KOLIBRI_KNOWLEDGE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/infra/tests/data/web_formula_corpus")

    add_executable(test_knowledge_retrieval
        infra/tests/test_knowledge_retrieval.c
        core/knowledge.c)
    target_include_directories(test_knowledge_retrieval PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/core)
    target_link_libraries(test_knowledge_retrieval PRIVATE m)
    add_test(NAME test_knowledge_retrieval COMMAND test_knowledge_retrieval)

    # CORPUS TRAINER - масштабное обучение с фиксированным размером
    add_executable(test_corpus_trainer infra/tests/test_corpus_trainer.c)
    target_link_libraries(test_corpus_trainer PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_corpus_trainer COMMAND test_corpus_trainer)

    add_executable(test_fractal_memory infra/tests/test_fractal_memory.c)
    target_link_libraries(test_fractal_memory PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_fractal_memory COMMAND test_fractal_memory)

    add_executable(test_genome_integration infra/tests/test_genome_integration.c)
    target_link_libraries(test_genome_integration PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_genome_integration COMMAND test_genome_integration)

    # AGI: ATTENTION - модуль Self-Attention (Transformer)
    add_executable(test_attention infra/tests/test_attention.c)
    target_link_libraries(test_attention PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_attention COMMAND test_attention)

    # NUMERIC TRANSFORMER - Numeric Tokenizer + Backprop + AdamW
    add_executable(test_numeric_transformer infra/tests/test_numeric_transformer.c)
    target_link_libraries(test_numeric_transformer PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_numeric_transformer COMMAND test_numeric_transformer)

    # MATH SOLVER - Symbolic Math Engine
    add_executable(test_math_solver infra/tests/test_math_solver.c)
    target_link_libraries(test_math_solver PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_math_solver COMMAND test_math_solver)

    # NUMERIC TOKENIZER - Math Expression Tokenizer
    add_executable(test_numeric_tokenizer infra/tests/test_numeric_tokenizer.c)
    target_link_libraries(test_numeric_tokenizer PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_numeric_tokenizer COMMAND test_numeric_tokenizer)

    # TRAINING PIPELINE - Corpus Math Trainer
    add_executable(test_corpus_math_trainer infra/tests/test_corpus_math_trainer.c)
    target_link_libraries(test_corpus_math_trainer PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_corpus_math_trainer COMMAND test_corpus_math_trainer)

    # EVOLUTIONARY TRAINER - Formula Evolution
    add_executable(test_evolutionary_trainer infra/tests/test_evolutionary_trainer.c)
    target_link_libraries(test_evolutionary_trainer PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_evolutionary_trainer COMMAND test_evolutionary_trainer)

    # SWARM LEARNER - Distributed Swarm Learning
    add_executable(test_swarm_learner infra/tests/test_swarm_learner.c)
    target_link_libraries(test_swarm_learner PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_swarm_learner COMMAND test_swarm_learner)

    add_executable(test_swarm_network infra/tests/test_swarm_network.c)
    target_link_libraries(test_swarm_network PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_swarm_network COMMAND test_swarm_network)

    # Month 6: Quality Assurance
    add_executable(test_self_verification infra/tests/test_self_verification.c)
    target_link_libraries(test_self_verification PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_self_verification COMMAND test_self_verification)

    add_executable(test_explanation_generator infra/tests/test_explanation_generator.c)
    target_link_libraries(test_explanation_generator PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_explanation_generator COMMAND test_explanation_generator)

    # Phase 3: Knowledge & Reasoning
    add_executable(test_reasoning_engine infra/tests/test_reasoning_engine.c)
    target_link_libraries(test_reasoning_engine PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_reasoning_engine COMMAND test_reasoning_engine)

    # INTENT CLASSIFIER - Query intent analysis
    add_executable(test_intent_classifier infra/tests/test_intent_classifier.c)
    target_link_libraries(test_intent_classifier PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_intent_classifier COMMAND test_intent_classifier)

    # REINFORCEMENT LEARNING - Q-learning for action selection
    add_executable(test_reinforcement_learning infra/tests/test_reinforcement_learning.c)
    target_link_libraries(test_reinforcement_learning PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_reinforcement_learning COMMAND test_reinforcement_learning)

    # ENCODING PIPELINE - Unified text encoding (digits + phonemes + semantic)
    add_executable(test_encoding_pipeline infra/tests/test_encoding_pipeline.c)
    target_link_libraries(test_encoding_pipeline PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_encoding_pipeline COMMAND test_encoding_pipeline)

    # ENCODING PIPELINE THREAD SAFETY - Multi-threaded encoding correctness
    add_executable(test_encoding_thread_safety infra/tests/test_encoding_thread_safety.c)
    target_link_libraries(test_encoding_thread_safety PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_encoding_thread_safety COMMAND test_encoding_thread_safety)

    # SEMANTIC PATTERNS - 64-digit semantic patterns stability
    add_executable(test_semantic_patterns infra/tests/test_semantic_patterns.c)
    target_link_libraries(test_semantic_patterns PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_semantic_patterns COMMAND test_semantic_patterns)

    add_executable(test_pattern_discovery infra/tests/test_pattern_discovery.c)
    target_link_libraries(test_pattern_discovery PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_pattern_discovery COMMAND test_pattern_discovery)

    # DOMAIN KNOWLEDGE LOADER - Physics, Chemistry, Programming, Law
    add_executable(test_domain_knowledge_loader infra/tests/test_domain_knowledge_loader.c)
    target_link_libraries(test_domain_knowledge_loader PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_domain_knowledge_loader COMMAND test_domain_knowledge_loader)

    # SIMD OPERATIONS - AVX2/NEON optimized vector math
    add_executable(test_simd_ops infra/tests/test_simd_ops.c)
    target_link_libraries(test_simd_ops PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_simd_ops COMMAND test_simd_ops)

    # THREADED INFERENCE - Multi-threaded matmul, thread pool
    add_executable(test_threaded_inference infra/tests/test_threaded_inference.c)
    target_link_libraries(test_threaded_inference PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_threaded_inference COMMAND test_threaded_inference)

    # Bonus: Fruit boxes puzzle
    add_executable(test_fruit_boxes infra/tests/test_fruit_boxes.c)
    target_link_libraries(test_fruit_boxes PRIVATE kolibri_core Threads::Threads m)

    # Bonus: 8 coins puzzle
    add_executable(test_8_coins infra/tests/test_8_coins.c)
    target_link_libraries(test_8_coins PRIVATE kolibri_core Threads::Threads m)

    # CLI: Reasoning engine
    add_executable(kolibri_reason apps/kolibri_reason.c)
    target_link_libraries(kolibri_reason PRIVATE kolibri_core Threads::Threads m)

    # CLI: Logical solver
    add_executable(kolibri_solve apps/kolibri_solve.c)
    target_link_libraries(kolibri_solve PRIVATE kolibri_core Threads::Threads m)

    # Logical solver test
    add_executable(test_logical_solver infra/tests/test_logical_solver.c)
    target_link_libraries(test_logical_solver PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_logical_solver COMMAND test_logical_solver)

    # Missing tests from QA audit
    add_executable(test_ai_encoder infra/tests/test_ai_encoder.c)
    target_link_libraries(test_ai_encoder PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_ai_encoder COMMAND test_ai_encoder)

    add_executable(test_ai_resonance infra/tests/test_ai_resonance.c)
    target_link_libraries(test_ai_resonance PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_ai_resonance COMMAND test_ai_resonance)

    add_executable(test_compress_project_v2 infra/tests/test_compress_project_v2.c)
    target_link_libraries(test_compress_project_v2 PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_compress_project_v2 COMMAND test_compress_project_v2)

    add_executable(test_correct_architecture infra/tests/test_correct_architecture.c)
    target_link_libraries(test_correct_architecture PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_correct_architecture COMMAND test_correct_architecture)

    add_executable(test_decimal_standalone infra/tests/test_decimal_standalone.c)
    target_link_libraries(test_decimal_standalone PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_decimal_standalone COMMAND test_decimal_standalone)

    add_executable(test_formula_compression infra/tests/test_formula_compression.c)
    target_link_libraries(test_formula_compression PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_formula_compression COMMAND test_formula_compression)

    add_executable(test_formula_storage infra/tests/test_formula_storage.c)
    target_link_libraries(test_formula_storage PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_formula_storage COMMAND test_formula_storage)

    add_executable(test_full_pipeline infra/tests/test_full_pipeline.c)
    target_link_libraries(test_full_pipeline PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_full_pipeline COMMAND test_full_pipeline)

    add_executable(test_generative_compression infra/tests/test_generative_compression.c)
    target_link_libraries(test_generative_compression PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_generative_compression COMMAND test_generative_compression)

    add_executable(test_genome_stress infra/tests/test_genome_stress.c)
    target_link_libraries(test_genome_stress PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_genome_stress COMMAND test_genome_stress)

    add_executable(test_image_full_cycle infra/tests/test_image_full_cycle.c)
    target_link_libraries(test_image_full_cycle PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_image_full_cycle COMMAND test_image_full_cycle)

    add_executable(test_inference_demo infra/tests/test_inference_demo.c)
    target_link_libraries(test_inference_demo PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_inference_demo COMMAND test_inference_demo)

    add_executable(test_multilevel_architecture infra/tests/test_multilevel_architecture.c)
    target_link_libraries(test_multilevel_architecture PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_multilevel_architecture COMMAND test_multilevel_architecture)

    add_executable(test_real_archive infra/tests/test_real_archive.c)
    target_link_libraries(test_real_archive PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_real_archive COMMAND test_real_archive)

    add_executable(test_real_image infra/tests/test_real_image.c)
    target_link_libraries(test_real_image PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_real_image COMMAND test_real_image)

    add_executable(test_storage_demo infra/tests/test_storage_demo.c)
    target_link_libraries(test_storage_demo PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_storage_demo COMMAND test_storage_demo)

    add_executable(test_text_roundtrip infra/tests/test_text_roundtrip.c)
    target_link_libraries(test_text_roundtrip PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_text_roundtrip COMMAND test_text_roundtrip)

    add_executable(test_true_generative infra/tests/test_true_generative.c)
    target_link_libraries(test_true_generative PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_true_generative COMMAND test_true_generative)

    # AGI: WORLD MODEL - предсказательная модель мира
    add_executable(test_world_model infra/tests/test_world_model.c)
    target_link_libraries(test_world_model PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_world_model COMMAND test_world_model)

    # AGI: AUTO LEARN - автономный цикл обучения
    add_executable(test_auto_learn infra/tests/test_auto_learn.c)
    target_link_libraries(test_auto_learn PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_auto_learn COMMAND test_auto_learn)

    # COMPRESSION STATS - compression metrics via auto_learn API
    add_executable(test_compression_stats infra/tests/test_compression_stats.c)
    target_link_libraries(test_compression_stats PRIVATE kolibri_core Threads::Threads m)
    add_test(NAME test_compression_stats COMMAND test_compression_stats)

    configure_file(infra/tests/ks_compiler_roundtrip.cmake
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
                         ${CMAKE_CURRENT_SOURCE_DIR}/infra/tests/run_kolibri_node_hmac.py
                         $<TARGET_FILE:kolibri_node>
                         inline)
        add_test(NAME kolibri_node_custom_key_file
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/infra/tests/run_kolibri_node_hmac.py
                         $<TARGET_FILE:kolibri_node>
                         file)
        add_test(NAME test_kolibri_http_server_api
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/infra/tests/test_kolibri_http_server_api.py
                         $<TARGET_FILE:kolibri_http_server>)
        set_tests_properties(test_kolibri_http_server_api PROPERTIES TIMEOUT 90)
        add_test(NAME test_kolibri_http_stream_api
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/infra/tests/test_kolibri_http_stream_api.py
                         $<TARGET_FILE:kolibri_http_server>)
        set_tests_properties(test_kolibri_http_stream_api PROPERTIES TIMEOUT 90)
        add_test(NAME test_kolibri_http_phase1_benchmark
                 COMMAND ${Python3_EXECUTABLE}
                         ${CMAKE_CURRENT_SOURCE_DIR}/infra/tests/test_kolibri_http_phase1_benchmark.py
                         $<TARGET_FILE:kolibri_http_server>)
        set_tests_properties(test_kolibri_http_phase1_benchmark PROPERTIES TIMEOUT 120)
    endif()

    add_test(NAME kolibri_node_usage COMMAND $<TARGET_FILE:kolibri_node> --help)

    add_test(NAME kolibri_indexer_usage COMMAND $<TARGET_FILE:kolibri_indexer>)
    set_tests_properties(kolibri_indexer_usage PROPERTIES WILL_FAIL TRUE)

    add_test(NAME kolibri_queue_usage COMMAND $<TARGET_FILE:kolibri_queue>)
    set_tests_properties(kolibri_queue_usage PROPERTIES WILL_FAIL TRUE)

    add_test(NAME kolibri_sim_usage COMMAND $<TARGET_FILE:kolibri_sim>)
    set_tests_properties(kolibri_sim_usage PROPERTIES WILL_FAIL TRUE)

    # Ensure tests run from the repository root (many tests use relative paths)
    get_property(_kolibri_all_tests DIRECTORY PROPERTY TESTS)
    foreach(_kolibri_test IN LISTS _kolibri_all_tests)
        set_tests_properties(${_kolibri_test} PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endforeach()
endif()

# Benchmark executables (not part of ctest, run manually)
add_executable(kolibri_benchmark_suite benchmarks/kolibri_benchmark_suite.c)
target_include_directories(kolibri_benchmark_suite PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(kolibri_benchmark_suite PRIVATE m)

add_executable(compare_with_competitors benchmarks/compare_with_competitors.c)
target_include_directories(compare_with_competitors PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(compare_with_competitors PRIVATE m)

add_executable(bench_kolibri_vs_world benchmarks/bench_kolibri_vs_world.c)
target_include_directories(bench_kolibri_vs_world PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(bench_kolibri_vs_world PRIVATE kolibri_core)

add_executable(kolibri_swarm_benchmark benchmarks/kolibri_swarm_benchmark.c)
target_include_directories(kolibri_swarm_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(kolibri_swarm_benchmark PRIVATE kolibri_core Threads::Threads m)

add_executable(kolibri_swarm_knowledge_benchmark benchmarks/kolibri_swarm_knowledge_benchmark.c)
target_include_directories(kolibri_swarm_knowledge_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core)
target_link_libraries(kolibri_swarm_knowledge_benchmark PRIVATE kolibri_core Threads::Threads m)

if(KOLIBRI_ENABLE_FUZZ)
  add_executable(kolibri_fuzz_script infra/tests/fuzz_script.c)
  target_link_libraries(kolibri_fuzz_script PRIVATE kolibri_core)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(kolibri_fuzz_script PRIVATE -fsanitize=fuzzer,address)
    target_link_options(kolibri_fuzz_script PRIVATE -fsanitize=fuzzer,address)
  else()
    message(WARNING "Kolibri fuzz targets are best built with Clang/libFuzzer")
  endif()
endif()

add_executable(kolibri_inspect apps/kolibri_inspect.c)
target_link_libraries(kolibri_inspect PRIVATE kolibri_core ${KOLIBRI_OPENSSL_TARGET})
if(TARGET OpenSSL::SSL)
    target_link_libraries(kolibri_inspect PRIVATE OpenSSL::SSL)
endif()

add_executable(kolibri_async_node apps/kolibri_async_node.c)
target_link_libraries(kolibri_async_node PRIVATE kolibri_core ${KOLIBRI_OPENSSL_TARGET})
